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

#include <gtest/gtest.h>

#include <arrow/array.h>
#include <arrow/buffer.h>
#include <arrow/compute/api.h>
#include <arrow/result.h>

TEST(ArrowComputeTest, AddTest) {
  std::vector<int32_t> original_data{1, 2, 3, 4, 5, 6, 7};
  auto arr1 = std::make_shared<arrow::Int32Array>(7, arrow::Buffer::Wrap(original_data));
  auto arr2 = std::make_shared<arrow::Int32Array>(7, arrow::Buffer::Wrap(original_data));

  arrow::Datum datum;
  datum = arrow::compute::CallFunction("add", {arr1, arr2}).ValueOrDie();

  std::shared_ptr<arrow::Int32Array> result =
      std::static_pointer_cast<arrow::Int32Array>(std::move(datum).make_array());

  for (int i = 0; i < 7; i++) {
    ASSERT_FALSE(result->IsNull(i));
    ASSERT_EQ(result->Value(i), (i + 1) * 2);
  }
}

TEST(ArrowComputeTest, MultiplyTest) {
  std::vector<int32_t> original_data{1, 2, 3, 4, 5, 6, 7};
  auto arr1 = std::make_shared<arrow::Int32Array>(7, arrow::Buffer::Wrap(original_data));
  auto arr2 = std::make_shared<arrow::Int32Array>(7, arrow::Buffer::Wrap(original_data));

  arrow::Datum datum;
  datum = arrow::compute::Multiply(arr1, arr2).ValueOrDie();

  std::shared_ptr<arrow::Int32Array> result =
      std::static_pointer_cast<arrow::Int32Array>(std::move(datum).make_array());

  for (int i = 0; i < 7; i++) {
    ASSERT_FALSE(result->IsNull(i));
    ASSERT_EQ(result->Value(i), (i + 1) * (i + 1));
  }
}

TEST(ArrowComputeTest, SumTest) {
  std::vector<int32_t> original_data{1, 2, 3, 4, 5, 6, 7};
  auto arr1 = std::make_shared<arrow::Int32Array>(7, arrow::Buffer::Wrap(original_data));

  arrow::Datum datum;
  datum = arrow::compute::CallFunction("sum", {arr1}).ValueOrDie();

  ASSERT_EQ(datum.kind(), arrow::Datum::SCALAR);
  // seems we can not connvert to a Int32Scalar
  std::shared_ptr<arrow::Int32Scalar> result =
      std::static_pointer_cast<arrow::Int32Scalar>(std::move(datum).scalar());

  ASSERT_EQ(result->ToString(), "28");

  // ASSERT_EQ(result->value, 28); // this will be false, due to cast.
}
