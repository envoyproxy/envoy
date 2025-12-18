#include <openssl/ssl.h>
#include <ossl.h>


extern "C" STACK_OF(SSL_CIPHER) *SSL_CTX_get_ciphers(const SSL_CTX *ctx) {
  return (STACK_OF(SSL_CIPHER)*)ossl.ossl_SSL_CTX_get_ciphers(ctx);
}
