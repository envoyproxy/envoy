#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L1614
 * https://www.openssl.org/docs/man3.0/man3/SSL_get_tlsext_status_ocsp_resp.html
 */
void SSL_get0_ocsp_response(const SSL *ssl, const uint8_t **out, size_t *out_len) {
  unsigned char *data;
  long len = ossl.ossl_SSL_get_tlsext_status_ocsp_resp((SSL*)ssl, &data);

  if(len == -1) {
    *out = NULL;
    *out_len = 0;
    return;
  }

  *out = data;
  *out_len = len;
}