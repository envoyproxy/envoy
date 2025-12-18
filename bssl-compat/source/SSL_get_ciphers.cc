#include <openssl/ssl.h>
#include <ossl.h>


extern "C" STACK_OF(SSL_CIPHER) *SSL_get_ciphers(const SSL *ssl) {
  return reinterpret_cast<STACK_OF(SSL_CIPHER)*>(ossl.ossl_SSL_get_ciphers(ssl));
}
