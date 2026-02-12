#include <openssl/x509.h>
#include <ossl.h>


int X509_verify_cert(X509_STORE_CTX *ctx) {
  int result = ossl.ossl_X509_verify_cert(ctx);

  if (result != 1) {
    if (ossl.ossl_X509_STORE_CTX_get_error(ctx) == ossl_X509_V_ERR_CERT_CHAIN_TOO_LONG) {
      ossl.ossl_X509_STORE_CTX_set_error(ctx, ossl_X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);
    }
  }

  return result;
}
