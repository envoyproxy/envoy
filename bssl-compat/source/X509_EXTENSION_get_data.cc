#include <openssl/x509.h>
#include <ossl.h>


extern "C" ASN1_OCTET_STRING *X509_EXTENSION_get_data( const X509_EXTENSION *ne) {
  return ossl.ossl_X509_EXTENSION_get_data(const_cast<X509_EXTENSION*>(ne));
}
