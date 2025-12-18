#include <openssl/pem.h>
#include <ossl.h>


RSA *PEM_read_bio_RSAPrivateKey(BIO *out, RSA **x, pem_password_cb *cb, void *u) {
  // FIXME: Reimplement with: https://www.openssl.org/docs/man3.0/man3/OSSL_DECODER_from_bio.html
  return ossl.ossl_PEM_read_bio_RSAPrivateKey(out, x, cb, u);
}