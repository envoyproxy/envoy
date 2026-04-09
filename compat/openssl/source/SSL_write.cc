#include <openssl/ssl.h>
#include <ossl.h>


int SSL_write(SSL *ssl, const void *buf, int num) {
  ossl.ossl_ERR_clear_error();
  return ossl.ossl_SSL_write(ssl, buf, num);
}
