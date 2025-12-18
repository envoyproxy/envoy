#include <openssl/ssl.h>
#include <ossl.h>


extern "C" void SSL_set_cert_cb(SSL *ssl, int (*cb)(SSL *ssl, void *arg), void *arg) {
  ossl.ossl_SSL_set_cert_cb(ssl, cb, arg);
}