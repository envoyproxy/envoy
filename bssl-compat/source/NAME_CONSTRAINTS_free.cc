#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" void NAME_CONSTRAINTS_free(NAME_CONSTRAINTS *n) {
  ossl.ossl_NAME_CONSTRAINTS_free(n);
}
