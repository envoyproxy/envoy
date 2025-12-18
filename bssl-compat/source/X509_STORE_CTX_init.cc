#include <openssl/x509.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/557b80f1a3e599459367391540488c132a000d55/include/openssl/x509.h#L2841
 * https://www.openssl.org/docs/man3.0/man3/X509_STORE_CTX_init.html
 */
extern "C" int X509_STORE_CTX_init(X509_STORE_CTX *ctx, X509_STORE *store, X509 *x509, STACK_OF(X509) *chain) {
  if (store == nullptr) {
    return 0; // BoringSSL requires a non-null store
  }
  return ossl.ossl_X509_STORE_CTX_init(ctx, store, x509, reinterpret_cast<ossl_STACK_OF(ossl_X509)*>(chain));
}
