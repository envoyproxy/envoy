#include <openssl/base64.h>
#include <openssl/digest.h>
#include <openssl/evp.h>
#include <ossl.h>

/*
 * bssl: https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/curve25519.h#L90
 * ossl: None
 *
 * https://wiki.openssl.org/index.php/EVP_Signing_and_Verifying
 * https://datatracker.ietf.org/doc/html/rfc8032
 */
extern "C" int ED25519_verify(const uint8_t *message, size_t message_len,
                              const uint8_t signature[64],
                              const uint8_t public_key[32]) {

    // https://www.openssl.org/docs/man3.0/man3/ECDSA_SIG_new.html
    ossl_EVP_MD_CTX *ctx = ossl.ossl_EVP_MD_CTX_new();
    // The original openssl patch for jwt_verify_lib used length of absl::string_view.
    // bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, castToUChar(key), key.length()));
    //
    // https://github.com/maistra/jwt_verify_lib/blob/d602507895f21fdc22325f0466861ff5eac73bb8/src/verify.cc#L205
    // The public key size for EdDSA Ed25519 is 256 bits, so it seems logical to default to 32 here
    //
    // https://www.openssl.org/docs/man3.0/man3/EVP_PKEY_new_raw_public_key.html

    ossl_EVP_PKEY *pkey = ossl.ossl_EVP_PKEY_new_raw_public_key(ossl_EVP_PKEY_ED25519,
                                                                NULL, reinterpret_cast<const unsigned char *>(public_key),
                                                                32);
    int res = ossl.ossl_EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey);

    if (res == 1) {
        // for ED25519 signature length are 64 bytes
        // https://www.openssl.org/docs/man3.0/man3/EVP_DigestVerify.html
        res = ossl.ossl_EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char *>(signature),
                                         64,
                                         reinterpret_cast<const unsigned char *>(message),
                                         message_len);
    }

    return res;
}