git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard b87c80300647c2c0311c1489a104470e099f1531
cmake .
make
cp -r include/* $THIRDPARTY_BUILD/include
cp ssl/libssl.a $THIRDPARTY_BUILD/lib
cp crypto/libcrypto.a $THIRDPARTY_BUILD/lib
