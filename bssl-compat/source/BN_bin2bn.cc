#include <openssl/bn.h>
#include <ossl.h>
#include <ctype.h>

extern "C" BIGNUM *BN_bin2bn(const uint8_t *in, size_t len, BIGNUM *ret) {

   // OPENSSL_EXPORT BIGNUM *BN_bin2bn(const uint8_t *in, size_t len, BIGNUM *ret);


  BIGNUM *n =  ossl.ossl_BN_bin2bn(reinterpret_cast<const unsigned char*>(in), static_cast<int>(len), ret);
  return n;
}
