#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" void GENERAL_NAME_free(GENERAL_NAME *n) {
  ossl.ossl_GENERAL_NAME_free(n);
}
