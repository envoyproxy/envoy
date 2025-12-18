#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" void GENERAL_NAMES_free(GENERAL_NAMES *a) {
  ossl.ossl_GENERAL_NAMES_free(reinterpret_cast<ossl_GENERAL_NAMES*>(a));
}
