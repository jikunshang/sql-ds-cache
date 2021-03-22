// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <unistd.h>

#include <openssl/sha.h>

#include <arrow/util/logging.h>

#include "src/utils/PlasmaCacheManager.h"

namespace ape {

PlasmaCacheManager::PlasmaCacheManager(std::string file_path) : file_path_(file_path) {
  ARROW_LOG(INFO) << "plasma, init cache manager with path: " << file_path;

  std::shared_ptr<plasma::PlasmaClient> client = std::make_shared<plasma::PlasmaClient>();
  arrow::Status status = client->Connect("/tmp/plasmaStore", "", 0);

  if (status.ok()) {
    client_ = client;
    ARROW_LOG(INFO) << "plasma, cache manager initialized";

    char buff[1024];
    gethostname(buff, sizeof(buff));
    hostname = buff;
  } else {
    ARROW_LOG(WARNING) << "plasma, Connect failed: " << status.message();
  }
}

PlasmaCacheManager::~PlasmaCacheManager() {}

bool PlasmaCacheManager::connected() { return client_ != nullptr; }

void PlasmaCacheManager::release() {
  ARROW_LOG(INFO) << "plasma, release objects";

  if (!client_) {
    return;
  }

  for (auto oid : object_ids) {
    client_->Release(oid);
  }

  object_ids.clear();
}

void PlasmaCacheManager::close() {
  ARROW_LOG(INFO) << "plasma, close cache manager";

  if (!client_) {
    return;
  }

  // release objects
  release();

  // disconnct
  arrow::Status status = client_->Disconnect();
  if (!status.ok()) {
    ARROW_LOG(WARNING) << "plasma, Disconnect failed: " << status.message();
  }

  client_ = nullptr;

  // save cache info to redis
  setCacheInfoToRedis();
}

void PlasmaCacheManager::setCacheInfoToRedis() {
  if (redis_) {
    try {
      redis_->incrby("pmem_cache_global_cache_hit", cache_hit_count_);
      redis_->incrby("pmem_cache_global_cache_missed", cache_miss_count_);

      // save locations of cached data
      std::unordered_map<std::string, double> scores;
      char buff[1024];
      for (auto range : cached_ranges_) {
        snprintf(buff, sizeof(buff), "%d_%d_%s", range.offset, range.length,
                 hostname.c_str());
        std::string member = buff;
        scores.insert({member, range.offset});
      }

      if (scores.size() > 0) {
        redis_->zadd(file_path_, scores.begin(), scores.end());
      }

      cache_hit_count_ = 0;
      cache_miss_count_ = 0;
      cached_ranges_.clear();

      ARROW_LOG(INFO) << "plasma, saved cache info to redis";
    } catch (const sw::redis::Error& e) {
      ARROW_LOG(WARNING) << "plasma, save cache info to redis failed: " << e.what();
    }
  }
}

std::string PlasmaCacheManager::cacheKeyofFileRange(::arrow::io::ReadRange range) {
  char buff[1024];
  snprintf(buff, sizeof(buff), "plasma_cache:parquet_chunk:%s:%d_%d", file_path_.c_str(),
           range.offset, range.length);
  std::string ret = buff;
  return ret;
}

plasma::ObjectID PlasmaCacheManager::objectIdOfFileRange(::arrow::io::ReadRange range) {
  std::string cache_key = cacheKeyofFileRange(range);

  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((const unsigned char*)cache_key.c_str(), cache_key.length(), hash);

  return plasma::ObjectID::from_binary(std::string(hash, hash + sizeof(hash)));
}

void PlasmaCacheManager::setCacheRedis(
    std::shared_ptr<sw::redis::ConnectionOptions> options) {
  try {
    sw::redis::ConnectionOptions connection_options;
    connection_options.host = options->host;
    connection_options.port = options->port;
    connection_options.password = options->password;

    auto redis = std::make_shared<sw::redis::Redis>(connection_options);
    redis_ = redis;
    ARROW_LOG(INFO) << "plasma, set cache redis: " << options->host;
  } catch (const sw::redis::Error& e) {
    ARROW_LOG(WARNING) << "plasma, set redis failed: " << e.what();
  }
}

bool PlasmaCacheManager::containsFileRange(::arrow::io::ReadRange range) {
  bool has_object;

  arrow::Status status = client_->Contains(objectIdOfFileRange(range), &has_object);
  if (!status.ok()) {
    ARROW_LOG(WARNING) << "plasma, Contains failed: " << status.message();
    return false;
  }

  // we don't increase cache_hit_count_ here when has_object == true.
  // cache_hit_count_ will be updated in `get()`.
  if (!has_object) {
    cache_miss_count_ += 1;
  }

  return has_object;
}

std::shared_ptr<Buffer> PlasmaCacheManager::getFileRange(::arrow::io::ReadRange range) {
  std::vector<plasma::ObjectID> oids;
  plasma::ObjectID oid = objectIdOfFileRange(range);
  oids.push_back(oid);

  std::vector<plasma::ObjectBuffer> obufs(1);

  arrow::Status status = client_->Get(oids.data(), 1, 1000, obufs.data());
  if (!status.ok() || obufs[0].data == nullptr) {
    ARROW_LOG(WARNING) << "plasma, Get failed: " << status.message();
    cache_miss_count_ += 1;
    return nullptr;
  }

  // save object id for future release()
  object_ids.push_back(oid);

  cache_hit_count_ += 1;
  cached_ranges_.push_back(range);

  ARROW_LOG(DEBUG) << "plasma, get object from cache: " << file_path_ << ", "
                   << range.offset << ", " << range.length;

  return obufs[0].data;
}

bool PlasmaCacheManager::cacheFileRange(::arrow::io::ReadRange range,
                                        std::shared_ptr<Buffer> data) {
  std::vector<plasma::ObjectID> oids;
  plasma::ObjectID oid = objectIdOfFileRange(range);

  // create new object
  std::shared_ptr<Buffer> saved_data;
  Status status = client_->Create(oid, data->size(), nullptr, 0, &saved_data);
  if (plasma::IsPlasmaObjectExists(status)) {
    ARROW_LOG(WARNING) << "plasma, Create failed, PlasmaObjectExists: "
                       << status.message();
    return false;
  }
  if (plasma::IsPlasmaStoreFull(status)) {
    ARROW_LOG(WARNING) << "plasma, Create failed, PlasmaStoreFull: " << status.message();
    return false;
  }
  if (!status.ok()) {
    ARROW_LOG(WARNING) << "plasma, Create failed: " << status.message();
    return false;
  }

  // copy data
  memcpy(saved_data->mutable_data(), data->data(), data->size());

  // seal object
  status = client_->Seal(oid);
  if (!status.ok()) {
    ARROW_LOG(WARNING) << "plasma, Seal failed: " << status.message();

    // abort object
    status = client_->Abort(oid);
    if (!status.ok()) {
      ARROW_LOG(WARNING) << "plasma, Abort failed: " << status.message();
    }

    // release object
    status = client_->Release(oid);
    if (!status.ok()) {
      ARROW_LOG(WARNING) << "plasma, Release failed: " << status.message();
      return false;
    }

    return false;
  }

  // release object
  status = client_->Release(oid);
  if (!status.ok()) {
    ARROW_LOG(WARNING) << "plasma, Release failed: " << status.message();
    return false;
  }

  cached_ranges_.push_back(range);

  ARROW_LOG(DEBUG) << "plasma, object cached: " << file_path_ << ", " << range.offset
                   << ", " << range.length;

  return true;
}

bool PlasmaCacheManager::deleteFileRange(::arrow::io::ReadRange range) {
  arrow::Status status = client_->Delete(objectIdOfFileRange(range));
  if (!status.ok()) {
    ARROW_LOG(WARNING) << "plasma, Delete failed: " << status.message();
    return false;
  }

  ARROW_LOG(INFO) << "plasma, delete object from cache: " << file_path_ << ", "
                  << range.offset << ", " << range.length;
  return true;
}

PlasmaCacheManagerProvider::PlasmaCacheManagerProvider(std::string file_path)
    : file_path_(file_path) {
  auto default_manager = std::make_shared<PlasmaCacheManager>(file_path);
  managers_.push_back(default_manager);
}

PlasmaCacheManagerProvider::~PlasmaCacheManagerProvider() {}

void PlasmaCacheManagerProvider::close() {
  for (auto manager : managers_) {
    manager->close();
  }
}

bool PlasmaCacheManagerProvider::connected() { return managers_[0]->connected(); }

void PlasmaCacheManagerProvider::setCacheRedis(
    std::shared_ptr<sw::redis::ConnectionOptions> options) {
  redis_options_ = options;

  for (auto manager : managers_) {
    manager->setCacheRedis(options);
  }
}

std::shared_ptr<parquet::CacheManager> PlasmaCacheManagerProvider::defaultCacheManager() {
  return managers_[0];
}

std::shared_ptr<parquet::CacheManager> PlasmaCacheManagerProvider::newCacheManager() {
  auto new_manager = std::make_shared<PlasmaCacheManager>(file_path_);
  managers_.push_back(new_manager);

  if (redis_options_) {
    new_manager->setCacheRedis(redis_options_);
  }

  return new_manager;
}

}  // namespace ape
