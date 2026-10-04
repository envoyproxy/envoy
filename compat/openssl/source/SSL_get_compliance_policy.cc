#include <cstdint>

#include <openssl/ssl.h>
#include <ossl.h>

#include "ssl_compliance_policy.h"

// OpenSSL has no compliance-policy accessor. The policy set via
// SSL_set_compliance_policy() is stashed in SSL ex_data (see that file); read it
// back here. An SSL that never had a policy set returns nullptr ex_data, which
// maps to ssl_compliance_policy_none (the enum's zero value), matching
// BoringSSL's default.
enum ssl_compliance_policy_t SSL_get_compliance_policy(const SSL* ssl) {
  void* data = ossl.ossl_SSL_get_ex_data(ssl, sslCompliancePolicyExDataIndex());
  return static_cast<enum ssl_compliance_policy_t>(reinterpret_cast<intptr_t>(data));
}
