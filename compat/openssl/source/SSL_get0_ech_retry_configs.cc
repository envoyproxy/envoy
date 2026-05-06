#include <stddef.h>
#include <stdint.h>
#include <openssl/ssl.h>

extern "C" void SSL_get0_ech_retry_configs(const SSL* ssl, const uint8_t** out_retry_configs,
                                            size_t* out_retry_configs_len) {
  // ECH has no equivalent in OpenSSL; no retry configs available.
  (void)ssl;
  *out_retry_configs = nullptr;
  *out_retry_configs_len = 0;
}
