#include <openssl/ssl.h>
#include <ossl.h>


extern "C" STACK_OF(X509) *SSL_get_peer_cert_chain(const SSL *ssl) {
  return (STACK_OF(X509)*)ossl.ossl_SSL_get_peer_cert_chain(ssl);
}