#include <openssl/x509.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/557b80f1a3e599459367391540488c132a000d55/include/openssl/x509.h#L348
 * https://www.openssl.org/docs/man3.0/man3/X509_sign.html
 */
extern "C" int X509_sign(X509 *x509, EVP_PKEY *pkey, const EVP_MD *md) {
  return (ossl.ossl_X509_sign(x509, pkey, md) == 0) ? 0 : 1;
}
