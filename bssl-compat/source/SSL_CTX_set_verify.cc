#include <openssl/ssl.h>
#include <ossl.h>


extern "C" void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, int (*callback)(int ok, X509_STORE_CTX *store_ctx)) {
  ossl.ossl_SSL_CTX_set_verify(ctx, mode, callback);
}
