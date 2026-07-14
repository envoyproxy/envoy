#include <openssl/ssl.h>

extern "C" int SSL_was_key_usage_invalid(const SSL *ssl) {
  // This function does not exist in OpenSSL 3.5, and there is no equivalent.
  // OpenSSL always enforces key usage, so it is never "invalid".
  // Returning 0 means key usage was valid.
  (void)ssl;
  return 0;
}
