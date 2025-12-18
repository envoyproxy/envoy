#include <openssl/ssl.h>
#include <ossl.h>


extern "C" void SSL_CTX_set_cert_verify_callback(SSL_CTX *ctx, int (*callback)(X509_STORE_CTX *store_ctx, void *arg), void *arg) {
  ossl.ossl_SSL_CTX_set_cert_verify_callback(ctx, callback, arg);
}