cd /tmp
git clone https://github.com/oap-project/arrow.git
cd arrow
BRANCH=ape-dev
git checkout %BRANCH

cd cpp
mkdir build
cd build
ARROW_INSTALL_DIR=/usr
cmake -DCMAKE_INSTALL_PREFIX=$ARROW_INSTALL_DIR -DARROW_PARQUET=ON -DARROW_HDFS=ON -DARROW_FILESYSTEM=ON -DARROW_ORC=ON -DARROW_DEPENDENCY_SOURCE=BUNDLED ..
make -j
sudo make install
