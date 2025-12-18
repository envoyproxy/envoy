#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" GENERAL_NAMES * GENERAL_NAMES_new(void) {
  return reinterpret_cast<GENERAL_NAMES*>(ossl.ossl_GENERAL_NAMES_new());
}
