#include <openssl/x509.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/557b80f1a3e599459367391540488c132a000d55/include/openssl/x509.h#L2922
 * https://www.openssl.org/docs/man3.0/man3/X509_STORE_CTX_set_verify_cb.html
 */
extern "C" void X509_STORE_CTX_set_verify_cb(X509_STORE_CTX *ctx, int (*verify_cb)(int, X509_STORE_CTX *)) {
  ossl.ossl_X509_STORE_CTX_set_verify_cb(ctx, verify_cb);
}
