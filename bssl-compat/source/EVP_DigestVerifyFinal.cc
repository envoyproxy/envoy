#include <openssl/base64.h>
#include <ossl.h>

/*
 * bssl: https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/evp.h#L373C27-L373C27
 * ossl: https://www.openssl.org/docs/man3.0/man3/EVP_DigestVerifyFinal.html
 */
extern "C" int EVP_DigestVerifyFinal(EVP_MD_CTX *ctx, const uint8_t *sig,
                              size_t sig_len) {
  return ossl.ossl_EVP_DigestVerifyFinal(ctx, reinterpret_cast<const unsigned char *>(sig), sig_len);
}