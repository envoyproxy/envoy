#include <openssl/ssl.h>

extern "C" int SSL_CTX_set1_ech_keys(SSL_CTX* ctx, SSL_ECH_KEYS* keys) {
  // ECH has no equivalent in OpenSSL; return 0 to signal failure.
  (void)ctx;
  (void)keys;
  return 0;
}
