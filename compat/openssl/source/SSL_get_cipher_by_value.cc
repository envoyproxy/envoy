#include <openssl/ssl.h>
#include <ossl.h>
#include <arpa/inet.h>

static SSL *createSSL() {
  SSL_CTX *ctx = SSL_CTX_new(TLS_method());
  if (!ctx) {
    return nullptr;
  }
  SSL *ssl = SSL_new(ctx);
  SSL_CTX_free(ctx);
  return ssl;
}

const SSL_CIPHER *SSL_get_cipher_by_value(uint16_t value) {
  // Created once on first call (thread-safe since C++11), then shared by all later calls.
  // It is only used for read-only cipher table lookups
  static SSL *ssl = createSSL();
  if (!ssl) {
    return nullptr;
  }
  uint16_t nvalue = htons(value);
  return ossl.ossl_SSL_CIPHER_find(ssl, (const unsigned char *)&nvalue);
}
