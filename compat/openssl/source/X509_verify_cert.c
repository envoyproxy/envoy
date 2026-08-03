#include <openssl/x509.h>
#include <ossl.h>


int X509_verify_cert(X509_STORE_CTX *ctx) {
  int result = ossl.ossl_X509_verify_cert(ctx);

  if (result != 1) {
    if (ossl.ossl_X509_STORE_CTX_get_error(ctx) == ossl_X509_V_ERR_CERT_CHAIN_TOO_LONG) {
      ossl.ossl_X509_STORE_CTX_set_error(ctx, ossl_X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);
    }
    return result;
  }

  // BoringSSL always includes the root CA in the verified chain, but OpenSSL
  // with X509_V_FLAG_PARTIAL_CHAIN may stop at the first trusted cert.
  // Walk the chain upward through the trust store until we reach a self-signed root.
  STACK_OF(X509) *chain = (STACK_OF(X509) *)ossl.ossl_X509_STORE_CTX_get0_chain(ctx);
  if (chain && sk_X509_num(chain) > 0 &&
      ossl.ossl_X509_check_issued && ossl.ossl_X509_STORE_CTX_get1_issuer) {
    X509 *last = sk_X509_value(chain, sk_X509_num(chain) - 1);
    for (int depth = 0;
         depth < 32 && ossl.ossl_X509_check_issued(last, last) != ossl_X509_V_OK;
         depth++) {
      X509 *issuer = NULL;
      if (ossl.ossl_X509_STORE_CTX_get1_issuer(&issuer, ctx, last) != 1 || !issuer) {
        break;
      }
      if (issuer == last) {
        X509_free(issuer);
        break;
      }
      if (sk_X509_push(chain, issuer) == 0) {
        X509_free(issuer);
        break;
      }
      last = issuer;
    }
  }

  return result;
}
