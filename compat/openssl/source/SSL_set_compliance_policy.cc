#include <cstdint>

#include <openssl/ssl.h>
#include <ossl.h>

#include "iana_2_ossl_names.h"
#include "ssl_compliance_policy.h"

#define OPENSSL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

// This is the per-connection (SSL*) counterpart of SSL_CTX_set_compliance_policy
// (see SSL_CTX_set_compliance_policy.cc). OpenSSL has no SSL_set_compliance_policy
// equivalent, so the fips202205 policy is applied manually to the SSL object using
// the same parameters as the SSL_CTX version.

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

static int Configure(SSL* ssl) {
  // The IANA cipher names above must be translated to OpenSSL's names before
  // being passed to OpenSSL. kTLS12Ciphers uses no BoringSSL equal-preference
  // group syntax, so a plain translation is sufficient.
  std::string ciphers{iana_2_ossl_names(kTLS12Ciphers)};

  return
      // Section 3.1:
      // "Servers that support government-only applications shall be
      // configured to use TLS 1.2 and should be configured to use TLS 1.3
      // as well. These servers should not be configured to use TLS 1.1 and
      // shall not use TLS 1.0, SSL 3.0, or SSL 2.0.
      ossl.ossl_SSL_set_min_proto_version(ssl, TLS1_2_VERSION) &&
      ossl.ossl_SSL_set_max_proto_version(ssl, TLS1_3_VERSION) &&
      // Sections 3.3.1.1.1 and 3.3.1.1.2 are ambiguous about whether
      // HMAC-SHA-1 cipher suites are permitted with TLS 1.2. However, later the
      // Encrypt-then-MAC extension is required for all CBC cipher suites and so
      // it's easier to drop them.
      ossl.ossl_SSL_set_cipher_list(ssl, ciphers.c_str()) &&
      ossl.ossl_SSL_set1_groups(ssl, kGroups, OPENSSL_ARRAY_SIZE(kGroups)) &&
      ossl.ossl_SSL_set1_sigalgs_list(ssl, kSigAlgs);
}

} // namespace fips202205

// Shared SSL ex_data index used to remember the applied compliance policy so
// SSL_get_compliance_policy() can return it. No free callback is needed: the
// policy enum is stored inline in the pointer value, not as an allocation.
int sslCompliancePolicyExDataIndex() {
  static int index{ossl.ossl_SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr)};
  return index;
}

int SSL_set_compliance_policy(SSL* ssl, enum ssl_compliance_policy_t policy) {
  int result = 0;
  switch (policy) {
  case ssl_compliance_policy_fips_202205:
    result = fips202205::Configure(ssl);
    break;
  default:
    return 0;
  }
  if (result == 1) {
    // Remember the policy so SSL_get_compliance_policy() can report it.
    ossl.ossl_SSL_set_ex_data(ssl, sslCompliancePolicyExDataIndex(),
                              reinterpret_cast<void*>(static_cast<intptr_t>(policy)));
  }
  return result;
}
