#include <openssl/ssl.h>

extern "C" void SSL_set_enforce_rsa_key_usage(SSL *ssl, int enabled) {
  // OpenSSL always enforces RSA key usage checks, so this is a no-op.
  (void)ssl;
  (void)enabled;
}
