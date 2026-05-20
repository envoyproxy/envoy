#include <openssl/ssl.h>
#include <ossl.h>


extern "C" SSL_CTX *SSL_CTX_new(const SSL_METHOD *method) {
  ossl_SSL_CTX *ctx = ossl.ossl_SSL_CTX_new(method);

  if (ctx) {
    ossl.ossl_SSL_CTX_set_options(ctx, ossl_SSL_OP_IGNORE_UNEXPECTED_EOF);
  }

  return ctx;
}
