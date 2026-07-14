#include <openssl/pem.h>
#include <ossl.h>

static int no_password_cb(char*, int, int, void*) { return 0; }

extern "C" EVP_PKEY* PEM_read_bio_PrivateKey(BIO* bp, EVP_PKEY** x, pem_password_cb* cb, void* u) {
  if (cb == nullptr && u == nullptr) {
    cb = no_password_cb;
  }
  return ossl.ossl_PEM_read_bio_PrivateKey(bp, x, cb, u);
}
