#include <openssl/evp.h>
#include <ossl.h>


/*
 * BSSL: https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/evp.h#L171
 * OSSL: https://www.openssl.org/docs/man3.0/man3/EVP_PKEY_get0_EC_KEY.html
 */
extern "C" EC_KEY *EVP_PKEY_get0_EC_KEY(const EVP_PKEY *pkey) {
  return (EC_KEY*)ossl.ossl_EVP_PKEY_get0_EC_KEY(pkey);
}
