#include <openssl/ssl.h>
#include <ossl.h>

extern "C" void
SSL_CTX_set_cert_cb(SSL_CTX* ctx,
                    int (*callback)(SSL *ssl, void* arg), void* arg) {
  ossl.ossl_SSL_CTX_set_cert_cb(ctx, callback, arg);
}
