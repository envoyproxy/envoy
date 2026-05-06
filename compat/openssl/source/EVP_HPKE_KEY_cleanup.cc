#include <openssl/hpke.h>

extern "C" void EVP_HPKE_KEY_cleanup(EVP_HPKE_KEY* key) {
  // HPKE has no equivalent in OpenSSL; no-op.
  (void)key;
}
