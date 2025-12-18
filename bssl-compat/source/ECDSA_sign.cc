#include <openssl/ecdsa.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/cfafcd454fd01ebc40f1a7f43537dd306b7b64c3/include/openssl/ecdsa.h#L79
 * https://www.openssl.org/docs/man3.0/man3/ECDSA_sign.html
 */
extern "C" int ECDSA_sign(int type, const uint8_t *digest, size_t digest_len, uint8_t *sig, unsigned int *sig_len, const EC_KEY *key) {
  return ossl.ossl_ECDSA_sign(type, digest, digest_len, sig, sig_len, const_cast<EC_KEY*>(key));
}
