#include <openssl/ssl.h>

extern "C" SSL_ECH_KEYS* SSL_ECH_KEYS_new(void) {
  // ECH has no equivalent in OpenSSL; return nullptr to signal unsupported.
  return nullptr;
}
