#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#LL2773C20-L2773C58
 * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_set_tlsext_servername_callback.html
 */
extern "C" int SSL_CTX_set_tlsext_servername_callback(SSL_CTX *ctx, int (*callback)(SSL *ssl, int *out_alert, void *arg)) {
  return ossl.ossl_SSL_CTX_set_tlsext_servername_callback(ctx, callback);
}
