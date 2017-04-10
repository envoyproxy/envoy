set -e

wget -O googletest-1.8.0.tar.gz https://github.com/google/googletest/archive/release-1.8.0.tar.gz
tar xf googletest-1.8.0.tar.gz
cd googletest-release-1.8.0
cmake -DCMAKE_INSTALL_PREFIX:PATH=$THIRDPARTY_BUILD .
make install
