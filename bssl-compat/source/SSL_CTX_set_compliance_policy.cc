#include <openssl/ssl.h>
#include <ossl.h>

#include "iana_2_ossl_names.h"

#define OPENSSL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

namespace fips202205 {

// (References are to SP 800-52r2):

// Section 3.4.2.2
// "at least one of the NIST-approved curves, P-256 (secp256r1) and P384
// (secp384r1), shall be supported as described in RFC 8422."
//
// Section 3.3.1
// "The server shall be configured to only use cipher suites that are
// composed entirely of NIST approved algorithms"
// NID_secp256r1 not available in OpenSSL
// static const int kGroups[] = {SSL_GROUP_SECP256R1, SSL_GROUP_SECP384R1};
static const int kGroups[] = {NID_secp384r1};

static const char* kSigAlgs = {
    "rsa_pkcs1_sha256"  // SSL_SIGN_RSA_PKCS1_SHA256,
    ":rsa_pkcs1_sha384" // SSL_SIGN_RSA_PKCS1_SHA384,
    ":rsa_pkcs1_sha512" // SSL_SIGN_RSA_PKCS1_SHA512,
    // Table 4.1:
    // "The curve should be P-256 or P-384"
    ":ecdsa_secp256r1_sha256" // SSL_SIGN_ECDSA_SECP256R1_SHA256,
    ":ecdsa_secp384r1_sha384" // SSL_SIGN_ECDSA_SECP384R1_SHA384,
    ":rsa_pss_rsae_sha256"    // SSL_SIGN_RSA_PSS_RSAE_SHA256,
    ":rsa_pss_rsae_sha384"    // SSL_SIGN_RSA_PSS_RSAE_SHA384,
    ":rsa_pss_rsae_sha512"    // SSL_SIGN_RSA_PSS_RSAE_SHA512,
};

static const char kTLS12Ciphers[] = "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:"
                                    "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:"
                                    "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:"
                                    "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";

static int Configure(SSL_CTX* ctx) {
  // tls13_cipher_policy field not present in OpenSSL
  // ctx->tls13_cipher_policy = ssl_compliance_policy_fips_202205;

  return
      // Section 3.1:
      // "Servers that support government-only applications shall be
      // configured to use TLS 1.2 and should be configured to use TLS 1.3
      // as well. These servers should not be configured to use TLS 1.1 and
      // shall not use TLS 1.0, SSL 3.0, or SSL 2.0.
      ossl.ossl_SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) &&
      ossl.ossl_SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) &&
      // Sections 3.3.1.1.1 and 3.3.1.1.2 are ambiguous about whether
      // HMAC-SHA-1 cipher suites are permitted with TLS 1.2. However, later the
      // Encrypt-then-MAC extension is required for all CBC cipher suites and so
      // it's easier to drop them.
      SSL_CTX_set_strict_cipher_list(ctx, kTLS12Ciphers) &&
      ossl.ossl_SSL_CTX_set1_groups(ctx, kGroups, OPENSSL_ARRAY_SIZE(kGroups)) &&
      ossl.ossl_SSL_CTX_set1_sigalgs_list(ctx, kSigAlgs);
}

} // namespace fips202205

int SSL_CTX_set_compliance_policy(SSL_CTX* ctx, enum ssl_compliance_policy_t policy) {
  switch (policy) {
  case ssl_compliance_policy_fips_202205:
    return fips202205::Configure(ctx);
  default:
    return 0;
  }
}
