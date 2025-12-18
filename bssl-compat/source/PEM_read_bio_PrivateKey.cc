#include <openssl/pem.h>
#include <ossl.h>


extern "C" EVP_PKEY *PEM_read_bio_PrivateKey(BIO *bp, EVP_PKEY **x, pem_password_cb *cb, void *u) {
  return ossl.ossl_PEM_read_bio_PrivateKey(bp, x, cb, u);
}