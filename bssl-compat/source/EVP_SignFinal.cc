#include <openssl/evp.h>
#include <ossl.h>


int EVP_SignFinal(const EVP_MD_CTX *ctx, uint8_t *sig, unsigned int *out_sig_len, EVP_PKEY *pkey) {
  return ossl.ossl_EVP_SignFinal(const_cast<ossl_EVP_MD_CTX*>(ctx), sig, out_sig_len, pkey);
}
