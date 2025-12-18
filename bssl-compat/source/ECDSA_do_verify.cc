#include <openssl/ecdsa.h>
#include <ossl.h>


extern "C" int ECDSA_do_verify(const uint8_t *digest, size_t digest_len, const ECDSA_SIG *sig, const EC_KEY *key) {
  return ossl.ossl_ECDSA_do_verify(digest, digest_len, sig, const_cast<EC_KEY*>(key));
}
