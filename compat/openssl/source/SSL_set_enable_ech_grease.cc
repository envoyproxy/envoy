#include <openssl/ssl.h>

extern "C" void SSL_set_enable_ech_grease(SSL* ssl, int enable) {
  // ECH has no equivalent in OpenSSL; no-op.
  (void)ssl;
  (void)enable;
}
