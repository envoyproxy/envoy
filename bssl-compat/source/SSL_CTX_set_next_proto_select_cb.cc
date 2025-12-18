#include "openssl/ssl.h"
#include "ossl.h"


void SSL_CTX_set_next_proto_select_cb( SSL_CTX *ctx, int (*cb)(SSL *ssl, uint8_t **out, uint8_t *out_len, const uint8_t *in, unsigned in_len, void *arg), void *arg) {
#ifdef ossl_SSL_CTX_set_next_proto_select_cb
  return ossl_SSL_CTX_set_next_proto_select_cb(ctx, cb, arg);
#else
  return ossl.ossl_SSL_CTX_set_next_proto_select_cb(ctx, cb, arg);
#endif
}
