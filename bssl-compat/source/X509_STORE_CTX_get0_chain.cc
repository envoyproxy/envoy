#include <openssl/x509.h>
#include <ossl.h>


extern "C" STACK_OF(X509) *X509_STORE_CTX_get0_chain(const X509_STORE_CTX *ctx) {
  return reinterpret_cast<STACK_OF(X509)*>(ossl_X509_STORE_CTX_get0_chain(ctx));
}
