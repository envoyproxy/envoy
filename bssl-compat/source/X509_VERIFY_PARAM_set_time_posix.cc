#include <openssl/x509.h>
#include <ossl.h>


void X509_VERIFY_PARAM_set_time_posix(X509_VERIFY_PARAM *param, int64_t t) {
  return ossl.ossl_X509_VERIFY_PARAM_set_time(param, t);
}