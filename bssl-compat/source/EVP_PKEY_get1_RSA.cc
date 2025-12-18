#include <openssl/evp.h>
#include <ossl.h>


/*
 * BSSL: https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/evp.h#L162
 * OSSL: https://www.openssl.org/docs/man3.0/man3/EVP_PKEY_get1_RSA.html
 */
extern "C" RSA *EVP_PKEY_get1_RSA(const EVP_PKEY *pkey) {
  return ossl.ossl_EVP_PKEY_get1_RSA((EVP_PKEY*)pkey);
}
