#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" GENERAL_NAME * GENERAL_NAME_new(void) {
  return ossl.ossl_GENERAL_NAME_new();
}
