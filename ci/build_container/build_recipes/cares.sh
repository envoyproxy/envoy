wget https://github.com/c-ares/c-ares/archive/cares-1_12_0.tar.gz
tar xf cares-1_12_0.tar.gz
cd c-ares-cares-1_12_0
./buildconf
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
