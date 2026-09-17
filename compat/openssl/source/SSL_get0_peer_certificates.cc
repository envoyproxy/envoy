#include <openssl/ssl.h>
#include <ossl.h>

const STACK_OF(CRYPTO_BUFFER)* SSL_get0_peer_certificates(const SSL* ssl) {
  if(ossl.ossl_SSL_get0_peer_certificate(ssl) == NULL)
    return NULL;
  else {
    // Dummy buffer just to return a non null value
    static STACK_OF(CRYPTO_BUFFER)* criptoBuffer = sk_CRYPTO_BUFFER_new_null();
    return criptoBuffer;
  }
}
