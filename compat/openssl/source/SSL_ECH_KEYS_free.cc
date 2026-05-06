#include <openssl/ssl.h>

extern "C" void SSL_ECH_KEYS_free(SSL_ECH_KEYS* keys) {
  // ECH has no equivalent in OpenSSL; no-op.
  (void)keys;
}
