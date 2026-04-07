#include <openssl/x509.h>
#include <ossl.h>


int X509_set_serialNumber(X509 *x509, const ASN1_INTEGER *serial) {
  return ossl.ossl_X509_set_serialNumber(x509, const_cast<ASN1_INTEGER*>(serial));
}
