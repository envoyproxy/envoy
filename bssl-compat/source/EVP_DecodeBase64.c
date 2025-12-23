#include <openssl/base64.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/base64.h#L103
 */
int EVP_DecodeBase64(uint8_t *out, size_t *out_len, size_t max_out, const uint8_t *in, size_t in_len) {
  size_t decoded_len;

  if(EVP_DecodedLength(&decoded_len, in_len)) {
    if(decoded_len <= max_out) {
      decoded_len = ossl.ossl_EVP_DecodeBlock(out, in, in_len);
      if(decoded_len >= 0) {
        *out_len = decoded_len;
        return 1;
      }
    }
  }

  return 0;
}