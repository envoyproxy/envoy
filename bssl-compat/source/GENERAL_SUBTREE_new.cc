#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" GENERAL_SUBTREE * GENERAL_SUBTREE_new(void) {
  return ossl.ossl_GENERAL_SUBTREE_new();
}
