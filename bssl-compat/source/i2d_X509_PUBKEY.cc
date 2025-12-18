#include <openssl/x509.h>
#include <ossl/openssl/x509.h>
#include <ossl.h>


int i2d_X509_PUBKEY(const X509_PUBKEY *a, unsigned char **out) {
  return ossl.ossl_i2d_X509_PUBKEY(a, out);
}