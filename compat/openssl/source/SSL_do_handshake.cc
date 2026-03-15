#include <openssl/ssl.h>
#include <ossl.h>


int SSL_do_handshake(SSL *ssl) {
  ossl.ossl_ERR_clear_error();
  return ossl.ossl_SSL_do_handshake(ssl);
}
