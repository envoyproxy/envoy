#include <openssl/ssl.h>
#include <ossl.h>


extern "C" int SSL_read(SSL *ssl, void *buf, int num) {
  ossl.ossl_ERR_clear_error();
  return ossl.ossl_SSL_read(ssl, buf, num);
}
