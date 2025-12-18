#include <openssl/rsa.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/cd0b767492199a82c7e362d1a117e8c3fef6b943/include/openssl/rsa.h#L194
 * https://www.openssl.org/docs/man3.0/man3/RSA_generate_key_ex.html
 */
extern "C" int RSA_generate_key_ex(RSA *rsa, int bits, const BIGNUM *e, BN_GENCB *cb) {
  return ossl.ossl_RSA_generate_key_ex(rsa, bits, const_cast<BIGNUM*>(e), cb);
}
