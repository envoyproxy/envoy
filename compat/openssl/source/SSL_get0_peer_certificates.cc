#include <openssl/ssl.h>
#include <ossl.h>

const STACK_OF(CRYPTO_BUFFER)* SSL_get0_peer_certificates(const SSL* ssl) {
  X509* x509Temp = SSL_get_peer_certificate(ssl);
  if (x509Temp == NULL)
    return NULL;
  else {
    // Dummy buffer just to return a non null value
    static STACK_OF(CRYPTO_BUFFER)* criptoBuffer = sk_CRYPTO_BUFFER_new_null();
    return criptoBuffer;
  }
}
