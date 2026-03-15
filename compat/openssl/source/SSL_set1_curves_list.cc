#include <openssl/ssl.h>
#include <ossl.h>


int SSL_set1_curves_list(SSL *ssl, const char *curves) {
  // OpenSSL introduced a bug in 3.5, making SSL_set1_curves_list() return 1
  // (success) instead of 0 (fail) when passed an empty curves string. BoringSSL
  // returns 0 (failure) and so did OpenSSL prior to version 3.5, so we need a
  // check here to preserve the BoringSSL semantics.
  if (curves && curves[0] == '\0') {
    return 0;
  }
  return ossl.ossl_SSL_set1_curves_list(ssl, curves);
}
