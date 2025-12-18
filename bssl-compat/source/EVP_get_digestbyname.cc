#include <openssl/evp.h>
#include <ossl.h>

extern "C" {

const EVP_MD *EVP_get_digestbyname(const char *name) {
#ifdef ossl_EVP_get_digestbyname
  return ossl_EVP_get_digestbyname(name);
#else
  return ossl.ossl_EVP_get_digestbyname(name);
#endif
}
}
