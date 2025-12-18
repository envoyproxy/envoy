#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" void GENERAL_SUBTREE_free(GENERAL_SUBTREE *n) {
  ossl.ossl_GENERAL_SUBTREE_free(n);
}
