#include <stddef.h>
#include <stdint.h>
#include <openssl/ssl.h>

extern "C" int SSL_set1_ech_config_list(SSL* ssl, const uint8_t* ech_config_list,
                                         size_t ech_config_list_len) {
  // ECH has no equivalent in OpenSSL; return 0 to signal failure.
  (void)ssl;
  (void)ech_config_list;
  (void)ech_config_list_len;
  return 0;
}
