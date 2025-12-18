#include <openssl/pem.h>
#include <ossl.h>


extern "C" X509 *PEM_read_bio_X509_AUX(BIO *out, X509 **x, pem_password_cb *cb, void *u) {
  return ossl.ossl_PEM_read_bio_X509_AUX(out, x, cb, u);
}
