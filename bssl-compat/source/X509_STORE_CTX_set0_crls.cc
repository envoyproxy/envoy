#include <openssl/x509.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/557b80f1a3e599459367391540488c132a000d55/include/openssl/x509.h#L2911
 * https://www.openssl.org/docs/man3.0/man3/X509_STORE_CTX_set0_crls.html
 */
extern "C" void X509_STORE_CTX_set0_crls(X509_STORE_CTX *c, STACK_OF(X509_CRL) *sk) {
  ossl.ossl_X509_STORE_CTX_set0_crls(c, reinterpret_cast<ossl_STACK_OF(ossl_X509_CRL)*>(sk));
}
