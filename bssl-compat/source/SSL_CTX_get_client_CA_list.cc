#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L2706
 * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_get_client_CA_list.html
 */
extern "C" STACK_OF(X509_NAME) *SSL_CTX_get_client_CA_list(const SSL_CTX *ctx) {
  return reinterpret_cast<STACK_OF(X509_NAME)*>(ossl.ossl_SSL_CTX_get_client_CA_list(ctx));
}
