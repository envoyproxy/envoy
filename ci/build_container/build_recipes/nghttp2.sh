set -e

wget https://github.com/nghttp2/nghttp2/releases/download/v1.20.0/nghttp2-1.20.0.tar.gz
tar xf nghttp2-1.20.0.tar.gz
cd nghttp2-1.20.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
