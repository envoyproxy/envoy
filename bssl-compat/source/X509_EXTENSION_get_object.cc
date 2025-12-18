#include <openssl/x509.h>
#include <ossl.h>

ASN1_OBJECT* X509_EXTENSION_get_object(const X509_EXTENSION* ex) {
  return ossl.ossl_X509_EXTENSION_get_object(const_cast<X509_EXTENSION*>(ex));
}
