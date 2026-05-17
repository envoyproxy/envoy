#include <openssl/x509.h>
#include <ossl.h>

extern "C" int X509_verify(const X509 *x509, EVP_PKEY *pkey) {
  return ossl.ossl_X509_verify(const_cast<X509 *>(x509), pkey);
}
