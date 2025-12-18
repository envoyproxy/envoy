#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L5079
 * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_set_tlsext_status_cb.html
 */
int SSL_CTX_set_tlsext_status_cb(SSL_CTX *ctx, int (*callback)(SSL *ssl, void *arg)) {
  return ossl.ossl_SSL_CTX_set_tlsext_status_cb(ctx, callback);
}