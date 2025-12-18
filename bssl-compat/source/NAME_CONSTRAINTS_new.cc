#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" NAME_CONSTRAINTS * NAME_CONSTRAINTS_new(void) {
  return ossl.ossl_NAME_CONSTRAINTS_new();
}
