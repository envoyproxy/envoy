#include <openssl/rsa.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/cd0b767492199a82c7e362d1a117e8c3fef6b943/include/openssl/rsa.h#L240
 * https://www.openssl.org/docs/man3.0/man3/RSA_public_encrypt.html
 */
extern "C" int RSA_encrypt(RSA *rsa, size_t *out_len, uint8_t *out, size_t max_out, const uint8_t *in, size_t in_len, int padding) {
  if (max_out < RSA_size(rsa)) {
    return 0;
  }

  if (out_len == nullptr) {
    return 0;
  }

  int ret = ossl.ossl_RSA_public_encrypt(in_len, in, out, rsa, padding);
  if (ret == -1) {
    return 0;
  }

  *out_len = ret;

  return 1;
}
