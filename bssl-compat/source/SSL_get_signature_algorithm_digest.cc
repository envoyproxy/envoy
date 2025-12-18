#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L1095
 */
extern "C" const EVP_MD *SSL_get_signature_algorithm_digest(uint16_t sigalg) {
  switch (sigalg) {
    case SSL_SIGN_RSA_PKCS1_SHA1:         return EVP_sha1();
    case SSL_SIGN_RSA_PKCS1_SHA256:       return EVP_sha256();
    case SSL_SIGN_RSA_PKCS1_SHA384:       return EVP_sha384();
    case SSL_SIGN_RSA_PKCS1_SHA512:       return EVP_sha512();
    case SSL_SIGN_ECDSA_SHA1:             return EVP_sha1();
    case SSL_SIGN_ECDSA_SECP256R1_SHA256: return EVP_sha256();
    case SSL_SIGN_ECDSA_SECP384R1_SHA384: return EVP_sha384();
    case SSL_SIGN_ECDSA_SECP521R1_SHA512: return EVP_sha512();
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:    return EVP_sha256();
    case SSL_SIGN_RSA_PSS_RSAE_SHA384:    return EVP_sha384();
    case SSL_SIGN_RSA_PSS_RSAE_SHA512:    return EVP_sha512();
    case SSL_SIGN_ED25519:                return nullptr;
  }

  return nullptr;
}
