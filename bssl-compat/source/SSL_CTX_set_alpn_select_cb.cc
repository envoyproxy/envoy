#include <openssl/ssl.h>
#include <ossl.h>


extern "C" void SSL_CTX_set_alpn_select_cb(SSL_CTX *ctx, int (*cb)(SSL *ssl, const uint8_t **out, uint8_t *out_len, const uint8_t *in, unsigned in_len, void *arg), void *arg) {
  ossl.ossl_SSL_CTX_set_alpn_select_cb(ctx, cb, arg);
}
