#include <openssl/ssl.h>
#include <ossl.h>


extern "C" void SSL_enable_ocsp_stapling(SSL *ssl) {
  ossl.ossl_SSL_set_tlsext_status_type(ssl, ossl_TLSEXT_STATUSTYPE_ocsp);
}

