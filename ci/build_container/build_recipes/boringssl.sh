set -e

git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard be2ee342d3781ddb954f91f8a7e660c6f59e87e5
cmake .
make
cp -r include/* $THIRDPARTY_BUILD/include
cp ssl/libssl.a $THIRDPARTY_BUILD/lib
cp crypto/libcrypto.a $THIRDPARTY_BUILD/lib
