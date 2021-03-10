
INSTALL_DIR=/usr

# install json
cd /tmp
git clone https://github.com/nlohmann/json
cd json
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
make -j
make install

# install hi-redis, redis-plus-plus
cd /tmp
git clone https://github.com/redis/hiredis.git
cd hiredis
make USE_SSL=1 PREFIX=$INSTALL_DIR install -j

cd /tmp
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
mkdir compile
cd compile
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR ..
make -j
make install


# install openssl-dev