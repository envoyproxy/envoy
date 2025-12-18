#include <openssl/ssl.h>
#include <ossl.h>


extern "C" const SSL_METHOD *TLS_with_buffers_method(void) {
  return ossl.ossl_TLS_method();
}

