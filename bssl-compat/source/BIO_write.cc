#include <openssl/bio.h>
#include <ossl.h>

int BIO_write(BIO* bio, const void* data, int len) {
  int result = ossl.ossl_BIO_write(bio, data, len);
  if (result == -1) {
    unsigned long err = ossl.ossl_ERR_peek_last_error();
    if (ossl_ERR_GET_LIB(err) == ossl_ERR_LIB_BIO &&
        ossl_ERR_GET_REASON(err) == ossl_BIO_R_UNINITIALIZED) {
      result = -2;
    }
  }
  return result;
}
