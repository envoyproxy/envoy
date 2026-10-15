#include <openssl/x509.h>
#include <ossl.h>

extern "C" int X509_CRL_get0_by_cert(X509_CRL *crl, X509_REVOKED **out,
                                     const X509 *x509) {
  return ossl.ossl_X509_CRL_get0_by_cert(crl, out, const_cast<X509 *>(x509));
}
