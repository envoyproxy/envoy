#include <openssl/x509.h>
#include <ossl.h>


extern "C" void X509_STORE_CTX_set0_trusted_stack(X509_STORE_CTX *ctx, STACK_OF(X509) *sk) {
  ossl.ossl_X509_STORE_CTX_set0_trusted_stack(ctx, reinterpret_cast<ossl_STACK_OF(ossl_X509)*>(sk));
}
