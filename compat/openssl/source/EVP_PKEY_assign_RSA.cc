#include <openssl/evp.h>
#include <ossl.h>

/*
 * OpenSSL's EVP_PKEY_assign_RSA() is a macro around the deprecated
 * EVP_PKEY_assign(), so call the latter via the ossl function pointer.
 */
extern "C" int EVP_PKEY_assign_RSA(EVP_PKEY* pkey, RSA* key) {
  return ossl.ossl_EVP_PKEY_assign(pkey, ossl_EVP_PKEY_RSA, key);
}
