#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L1709
 * https://www.openssl.org/docs/man3.0/man3/d2i_SSL_SESSION.html
 */
SSL_SESSION *SSL_SESSION_from_bytes(const uint8_t *in, size_t in_len, const SSL_CTX *ctx) {
  return ossl.ossl_d2i_SSL_SESSION(NULL, &in, in_len);
}
