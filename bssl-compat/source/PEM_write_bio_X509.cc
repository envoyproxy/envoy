#include <openssl/pem.h>
#include <ossl/openssl/pem.h>
#include <ossl.h>


extern "C" int PEM_write_bio_X509(BIO *bp, X509 *x) {
  return ossl.ossl_PEM_write_bio_X509(bp, x);
}
