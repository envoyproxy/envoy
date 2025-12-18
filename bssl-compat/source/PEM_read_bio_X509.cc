#include <openssl/pem.h>
#include <ossl/openssl/pem.h>
#include <ossl.h>


X509 *PEM_read_bio_X509(BIO *bp, X509 **x, pem_password_cb *cb, void *u) {
  return ossl.ossl_PEM_read_bio_X509(bp, x, cb, u);
}
