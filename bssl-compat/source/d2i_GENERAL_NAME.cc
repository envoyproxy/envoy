#include <openssl/x509v3.h>
#include <ossl.h>


extern "C" GENERAL_NAME *d2i_GENERAL_NAME(GENERAL_NAME **a, const unsigned char **in, long len) {
  return ossl.ossl_d2i_GENERAL_NAME(a, in, len);
}