set -e

wget -o - https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
tar xf libevent-2.1.8-stable.tar.gz
cd libevent-2.1.8-stable
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --disable-libevent-regress --disable-openssl
make install
