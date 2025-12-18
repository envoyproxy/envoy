#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L2216
 * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_set_tlsext_ticket_key_cb.html
 */
extern "C" int SSL_CTX_set_tlsext_ticket_key_cb(SSL_CTX *ctx, int (*callback)(SSL *ssl, uint8_t *key_name, uint8_t *iv, EVP_CIPHER_CTX *ctx, HMAC_CTX *hmac_ctx, int encrypt)) {
  return ossl.ossl_SSL_CTX_set_tlsext_ticket_key_cb(ctx, callback);
}
