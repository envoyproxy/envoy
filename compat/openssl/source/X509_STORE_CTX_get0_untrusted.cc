#include <openssl/x509.h>
#include <ossl.h>

extern "C" STACK_OF(X509)* X509_STORE_CTX_get0_untrusted(const X509_STORE_CTX* ctx) {
  return reinterpret_cast<STACK_OF(X509)*>(ossl.ossl_X509_STORE_CTX_get0_untrusted(ctx));
}
