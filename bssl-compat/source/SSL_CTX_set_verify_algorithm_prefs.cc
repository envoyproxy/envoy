#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L2639
 * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_set1_sigalgs.html
 */
extern "C" int SSL_CTX_set_verify_algorithm_prefs(SSL_CTX *ctx, const uint16_t *prefs, size_t num_prefs) {
  static const struct { // Copied from boringssl/ssl/ssl_privkey.cc
    int pkey_type;
    int hash_nid;
    uint16_t signature_algorithm;
  } kSignatureAlgorithmsMapping[] = {
      {EVP_PKEY_RSA, NID_sha1, SSL_SIGN_RSA_PKCS1_SHA1},
      {EVP_PKEY_RSA, NID_sha256, SSL_SIGN_RSA_PKCS1_SHA256},
      {EVP_PKEY_RSA, NID_sha384, SSL_SIGN_RSA_PKCS1_SHA384},
      {EVP_PKEY_RSA, NID_sha512, SSL_SIGN_RSA_PKCS1_SHA512},
      {EVP_PKEY_RSA_PSS, NID_sha256, SSL_SIGN_RSA_PSS_RSAE_SHA256},
      {EVP_PKEY_RSA_PSS, NID_sha384, SSL_SIGN_RSA_PSS_RSAE_SHA384},
      {EVP_PKEY_RSA_PSS, NID_sha512, SSL_SIGN_RSA_PSS_RSAE_SHA512},
      {EVP_PKEY_EC, NID_sha1, SSL_SIGN_ECDSA_SHA1},
      {EVP_PKEY_EC, NID_sha256, SSL_SIGN_ECDSA_SECP256R1_SHA256},
      {EVP_PKEY_EC, NID_sha384, SSL_SIGN_ECDSA_SECP384R1_SHA384},
      {EVP_PKEY_EC, NID_sha512, SSL_SIGN_ECDSA_SECP521R1_SHA512},
      {EVP_PKEY_ED25519, NID_undef, SSL_SIGN_ED25519},
  };
  static const int mmax = sizeof(kSignatureAlgorithmsMapping) /
                          sizeof(kSignatureAlgorithmsMapping[0]);

  int sigalgs[num_prefs * 2];

  for(size_t pi = 0; pi < num_prefs; pi++) {
    int mi;

    for(mi = 0; mi < mmax; mi++) {
      if(kSignatureAlgorithmsMapping[mi].signature_algorithm == prefs[pi]) {
        sigalgs[pi * 2] = kSignatureAlgorithmsMapping[mi].hash_nid;
        sigalgs[pi * 2 + 1] = kSignatureAlgorithmsMapping[mi].pkey_type;
        break;
      }
    }

    if(mi == mmax) {
      ossl_ERR_raise_data(ossl_ERR_LIB_SSL, ossl_ERR_R_INTERNAL_ERROR,
                        "unknown signature algorithm : %d", prefs[pi]);
      return 0;
    }
  }

  return ossl.ossl_SSL_CTX_set1_sigalgs(ctx, sigalgs, num_prefs * 2);
}
