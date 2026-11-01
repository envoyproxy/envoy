#include <openssl/evp.h>
#include <ossl.h>

/*
 * OpenSSL's EVP_PKEY_assign_EC_KEY() is a macro around the deprecated
 * EVP_PKEY_assign(), so call the latter via the ossl function pointer.
 */
extern "C" int EVP_PKEY_assign_EC_KEY(EVP_PKEY* pkey, EC_KEY* key) {
  return ossl.ossl_EVP_PKEY_assign(pkey, ossl_EVP_PKEY_EC, key);
}
