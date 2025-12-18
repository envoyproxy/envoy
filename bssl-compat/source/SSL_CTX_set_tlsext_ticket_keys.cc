#include <openssl/ssl.h>
#include <ossl.h>


extern "C" int SSL_CTX_set_tlsext_ticket_keys(SSL_CTX *ctx, const void *in, size_t len) {
  return ossl.ossl_SSL_CTX_set_tlsext_ticket_keys(ctx, (void*)in, len);
}
