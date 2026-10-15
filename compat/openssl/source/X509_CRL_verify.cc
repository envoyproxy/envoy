#include <openssl/x509.h>
#include <ossl.h>

extern "C" int X509_CRL_verify(const X509_CRL *crl, EVP_PKEY *pkey) {
  return ossl.ossl_X509_CRL_verify(const_cast<X509_CRL *>(crl), pkey);
}
