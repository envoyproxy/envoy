#include <openssl/hpke.h>

extern "C" const EVP_HPKE_KEM* EVP_hpke_x25519_hkdf_sha256(void) {
  // HPKE has no equivalent in OpenSSL; return nullptr to signal unsupported.
  return nullptr;
}
