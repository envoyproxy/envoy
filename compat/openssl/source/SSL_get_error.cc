#include <openssl/ssl.h>
#include <ossl.h>

extern "C" int SSL_get_error(const SSL *ssl, int ret_code) {
  int err = ossl.ossl_SSL_get_error(ssl, ret_code);

  // OpenSSL returns SSL_ERROR_WANT_CLIENT_HELLO_CB when the client hello
  // callback (set via SSL_CTX_set_client_hello_cb) returns SSL_CLIENT_HELLO_RETRY.
  // This constant does not exist in BoringSSL. Translate it to the BoringSSL
  // equivalent so that Envoy code only needs to handle BoringSSL error codes.
  if (err == ossl_SSL_ERROR_WANT_CLIENT_HELLO_CB) {
    return SSL_ERROR_PENDING_CERTIFICATE;
  }

  return err;
}
