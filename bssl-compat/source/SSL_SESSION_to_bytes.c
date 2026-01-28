#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L1698
 * https://www.openssl.org/docs/man3.0/man3/i2d_SSL_SESSION.html
 */
int SSL_SESSION_to_bytes(const SSL_SESSION *in, uint8_t **out_data, size_t *out_len) {
  if(in == NULL || out_data == NULL || out_len == NULL) {
    return 0;
  }

  int buflen = ossl.ossl_i2d_SSL_SESSION(in, NULL);
  if(buflen == 0) {
    return 0;
  }

  unsigned char *buf = OPENSSL_malloc(buflen);
  if(buf == NULL) {
    return 0;
  }

  buflen = ossl.ossl_i2d_SSL_SESSION(in, &buf);
  if(buflen == 0) {
    return 0;
  }

  *out_data = buf - buflen;
  *out_len = buflen;

  return 1;
}
