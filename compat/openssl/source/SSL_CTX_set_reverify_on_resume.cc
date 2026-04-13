#include <openssl/ssl.h>

extern "C" void SSL_CTX_set_reverify_on_resume(SSL_CTX *ctx, int enabled) {
  // This function does not exist in OpenSSL 3.5, and there is no equivalent.
  // It is a BoringSSL-specific hint to reverify certificates on session resumption.
  // No-op is safe here.
  (void)ctx;
  (void)enabled;
}
