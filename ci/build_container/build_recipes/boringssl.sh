set -e

git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard be873e9f48b2a07269300282b69bb17d496c69ee
cmake .
make
cp -r include/* $THIRDPARTY_BUILD/include
cp ssl/libssl.a $THIRDPARTY_BUILD/lib
cp crypto/libcrypto.a $THIRDPARTY_BUILD/lib
