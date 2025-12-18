#include <openssl/ecdsa.h>
#include <ossl.h>

extern "C" int ECDSA_verify(int type, const uint8_t *digest, size_t digest_len,
                 const uint8_t *sig, size_t sig_len, const EC_KEY *eckey) {
  return ossl.ossl_ECDSA_verify(type, digest, digest_len, sig, sig_len, (EC_KEY *)eckey);
}
