#include <openssl/x509.h>
#include <ossl.h>


EVP_PKEY *X509_get_pubkey(const X509 *x509) {
  return ossl.ossl_X509_get_pubkey(const_cast<X509*>(x509));
}
