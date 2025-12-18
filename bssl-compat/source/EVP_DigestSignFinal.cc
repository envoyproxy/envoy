#include <openssl/evp.h>
#include <ossl.h>

extern "C" {

// EVP_DigestSignFinal and EVP_DigestSignUdpate have identical arity and types and are auto-mapped

OPENSSL_EXPORT int EVP_DigestSignFinal(EVP_MD_CTX *ctx, uint8_t *out_sig,
                                       size_t *out_sig_len) {
    return ossl.ossl_EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char *>(out_sig), out_sig_len);
}

}
