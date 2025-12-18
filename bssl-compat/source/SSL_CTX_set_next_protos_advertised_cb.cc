#include "openssl/ssl.h"
#include "ossl.h"

extern "C" {
void SSL_CTX_set_next_protos_advertised_cb( SSL_CTX *ctx, int (*cb)(SSL *ssl, const uint8_t **out, unsigned *out_len, void *arg), void *arg) {
#ifdef ossl_SSL_CTX_set_next_protos_advertised_cb
  return ossl_SSL_CTX_set_next_protos_advertised_cb(ctx, cb, arg);
#else
  return ossl.ossl_SSL_CTX_set_next_protos_advertised_cb(ctx, cb, arg);
#endif
}
}