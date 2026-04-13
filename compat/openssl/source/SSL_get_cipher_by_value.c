#include <openssl/ssl.h>
#include <ossl.h>
#include <arpa/inet.h>


const SSL_CIPHER *SSL_get_cipher_by_value(uint16_t value) {
  const SSL_CIPHER *result = NULL;
  SSL_CTX *ctx = SSL_CTX_new(TLS_method());
  if(ctx) {
    SSL *ssl = SSL_new(ctx);
    if(ssl) {
      uint16_t nvalue = htons(value);
      result = ossl.ossl_SSL_CIPHER_find(ssl, (const unsigned char*)&nvalue);
      SSL_free(ssl);
    }
    SSL_CTX_free(ctx);
  }

  return result;
}
