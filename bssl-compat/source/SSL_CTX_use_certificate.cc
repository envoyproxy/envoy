#include <openssl/ssl.h>
#include <ossl.h>


extern "C" int SSL_CTX_use_certificate(SSL_CTX *ctx, X509 *x509) {
  int ret = ossl.ossl_SSL_CTX_use_certificate(ctx, x509);
  return (ret == 1) ? 1 : 0;
}
