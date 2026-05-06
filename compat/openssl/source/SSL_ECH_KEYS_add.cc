#include <stddef.h>
#include <stdint.h>
#include <openssl/ssl.h>
#include <openssl/hpke.h>

extern "C" int SSL_ECH_KEYS_add(SSL_ECH_KEYS* keys, int is_retry_config,
                                 const uint8_t* ech_config, size_t ech_config_len,
                                 const EVP_HPKE_KEY* key) {
  // ECH has no equivalent in OpenSSL; return 0 to signal failure.
  (void)keys;
  (void)is_retry_config;
  (void)ech_config;
  (void)ech_config_len;
  (void)key;
  return 0;
}
