#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L2665
 * https://www.openssl.org/docs/man3.0/man3/SSL_set_client_CA_list.html
 */
extern "C" void SSL_set_client_CA_list(SSL *ssl, STACK_OF(X509_NAME) *name_list) {
  ossl.ossl_SSL_set_client_CA_list(ssl, reinterpret_cast<ossl_STACK_OF(ossl_X509_NAME)*>(name_list));
}
