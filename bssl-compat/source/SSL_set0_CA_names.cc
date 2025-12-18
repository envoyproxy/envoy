#include <openssl/ssl.h>
#include <ossl.h>


void SSL_set0_CA_names(SSL *ssl, STACK_OF(CRYPTO_BUFFER) *name_list) {
 // BoringSSL code
 // if (!ssl->config) {
 //   return;
 // }
 // ssl->config->CA_names.reset(name_list);

  // TODO
  // perhaps using:
  ///SSL_CTX_set0_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list)
}
