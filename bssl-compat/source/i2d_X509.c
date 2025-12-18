#include <openssl/ssl.h>
#include <ossl.h>


/*
 * On error, it returns a negative value. On success, it returns the length of
 * the result and outputs it via outp as follows:
 * 
 * If outp is NULL, the function writes nothing. This mode can be used to size
 * buffers. 
 * 
 * If outp is non-NULL but *outp is NULL, the function sets *outp to a newly
 * allocated buffer containing the result. The caller is responsible for
 * releasing *outp with OPENSSL_free. This mode is recommended for most callers.
 * 
 * If outp and *outp are non-NULL, the function writes the result to *outp,
 * which must have enough space available, and advances *outp just past the
 * output.
 */
int i2d_X509(X509 *x509, uint8_t **outp) {
  ossl_BIO *bio = ossl.ossl_BIO_new(ossl.ossl_BIO_s_mem());
  int length = -1;

  if (ossl.ossl_i2d_X509_bio(bio, x509)) { // 1=success,0=failure
    char *buf = NULL;
    length = ossl.ossl_BIO_get_mem_data(bio, &buf);

    if (outp) {
      if (*outp == NULL) {
        *outp = ossl.ossl_OPENSSL_memdup(buf, length);
      }
      else {
        ossl.ossl_OPENSSL_strlcpy((char*)*outp, buf, length);
      }
    }
  }

  ossl.ossl_BIO_free(bio);

  return length;
}
