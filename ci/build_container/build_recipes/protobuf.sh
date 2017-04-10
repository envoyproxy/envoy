set -e

wget https://github.com/google/protobuf/releases/download/v3.2.0/protobuf-cpp-3.2.0.tar.gz
tar xf protobuf-cpp-3.2.0.tar.gz
rsync -av protobuf-3.2.0 $THIRDPARTY_SRC
cd protobuf-3.2.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make install
