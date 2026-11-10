#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <ossl.h>

#include "openssl/base.h"

static SSL* createSSL() {
  SSL_CTX* ctx{SSL_CTX_new(TLS_method())};
  if (!ctx) {
    return nullptr;
  }
  SSL* ssl{SSL_new(ctx)};
  SSL_CTX_free(ctx);
  return ssl;
}

const SSL_CIPHER* SSL_get_cipher_by_value(uint16_t value) {
  // OpenSSL does not guarantee that SSL_CIPHER_find() is thread safe, so each thread creates its
  // own SSL object on first call and reuses it for later lookups. It is freed at thread exit.
  static thread_local bssl::UniquePtr<SSL> ssl{createSSL()};
  if (!ssl) {
    return nullptr;
  }
  uint16_t nvalue{htons(value)};
  return ossl.ossl_SSL_CIPHER_find(ssl.get(), (const unsigned char*)&nvalue);
}
