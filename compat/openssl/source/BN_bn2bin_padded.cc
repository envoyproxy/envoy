#include <openssl/bn.h>
#include <ossl.h>

/*
 * BoringSSL: int BN_bn2bin_padded(uint8_t *out, size_t len, const BIGNUM *in);
 * OpenSSL:   int BN_bn2binpad(const BIGNUM *a, unsigned char *to, int tolen);
 *
 * BoringSSL returns 1 on success, 0 if |in| is too large.
 * OpenSSL returns the number of bytes written on success, -1 on error.
 */
extern "C" int BN_bn2bin_padded(uint8_t* out, size_t len, const BIGNUM* in) {
  int rc = ossl.ossl_BN_bn2binpad(in, out, static_cast<int>(len));
  return rc >= 0 ? 1 : 0;
}
