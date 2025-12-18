#include <openssl/base64.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/base64.h#L97
 */
int EVP_DecodedLength(size_t *out_len, size_t len) {
  if (len % 4 != 0) {
    return 0;
  }

  *out_len = (len / 4) * 3;

  return 1;
}