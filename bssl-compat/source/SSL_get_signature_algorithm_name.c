#include <openssl/ssl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#LL1086C16-L1087C80
 */
const char *SSL_get_signature_algorithm_name(uint16_t sigalg, int include_curve) {
  switch (sigalg) {
    case SSL_SIGN_RSA_PKCS1_SHA1:         return "rsa_pkcs1_sha1";
    case SSL_SIGN_RSA_PKCS1_SHA256:       return "rsa_pkcs1_sha256";
    case SSL_SIGN_RSA_PKCS1_SHA384:       return "rsa_pkcs1_sha384";
    case SSL_SIGN_RSA_PKCS1_SHA512:       return "rsa_pkcs1_sha512";
    case SSL_SIGN_ECDSA_SHA1:             return "ecdsa_sha1";
    case SSL_SIGN_ECDSA_SECP256R1_SHA256: return include_curve ? "ecdsa_secp256r1_sha256" : "ecdsa_sha256";
    case SSL_SIGN_ECDSA_SECP384R1_SHA384: return include_curve ? "ecdsa_secp384r1_sha384" : "ecdsa_sha384";
    case SSL_SIGN_ECDSA_SECP521R1_SHA512: return include_curve ? "ecdsa_secp521r1_sha512" : "ecdsa_sha512";
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:    return "rsa_pss_rsae_sha256";
    case SSL_SIGN_RSA_PSS_RSAE_SHA384:    return "rsa_pss_rsae_sha384";
    case SSL_SIGN_RSA_PSS_RSAE_SHA512:    return "rsa_pss_rsae_sha512";
    case SSL_SIGN_ED25519:                return "ed25519";
  }

  return NULL;
}
