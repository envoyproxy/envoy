#include <openssl/x509.h>
#include <ossl.h>


const uint8_t *X509_alias_get0(const X509 *x509, int *out_len) {
  return ossl.ossl_X509_alias_get0(const_cast<X509*>(x509), out_len);
}
