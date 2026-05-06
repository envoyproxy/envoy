#include <openssl/ssl.h>

extern "C" const char* SSL_get0_ech_name_override(const SSL* ssl) {
  // ECH has no equivalent in OpenSSL; no name override available.
  (void)ssl;
  return nullptr;
}
