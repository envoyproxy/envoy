#include <stddef.h>
#include <stdint.h>
#include <openssl/hpke.h>

extern "C" int EVP_HPKE_KEY_init(EVP_HPKE_KEY* key, const EVP_HPKE_KEM* kem,
                                  const uint8_t* priv_key, size_t priv_key_len) {
  // HPKE key initialization has no equivalent in OpenSSL; return 0 to signal failure.
  (void)key;
  (void)kem;
  (void)priv_key;
  (void)priv_key_len;
  return 0;
}
