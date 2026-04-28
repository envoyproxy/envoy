#pragma once

#include <openssl/ssl.h>

// Aws-lc can be utilized as an alternative to boringssl
// This file provides API translation from boringssl to aws-lc when Envoy is compiled with aws-lc
// As of now, aws-lc is only compiled with Envoy for the ``ppc64le`` platform
// More information about aws-lc can be found here: https://github.com/aws/aws-lc
// This file should be included wherever the following identifiers are invoked by Envoy

namespace Envoy {

#ifdef OPENSSL_IS_AWSLC
#define sk_X509_NAME_find sk_X509_NAME_find_awslc

// AWS-LC does not support BoringSSL's compliance policy API.
// Define the enum and a stub that returns failure so call sites
// can handle it through normal error paths without #ifdef guards.
enum ssl_compliance_policy_t {
  ssl_compliance_policy_fips_202205 = 1,
};

inline int SSL_CTX_set_compliance_policy(SSL_CTX* /*ctx*/,
                                         enum ssl_compliance_policy_t /*policy*/) {
  return 0;
}

// AWS-LC's X509_NAME_dup takes non-const X509_NAME*, unlike BoringSSL which accepts const.
// Wrap it so call sites stay const-correct for other targets.
inline X509_NAME* X509_NAME_dup(const X509_NAME* name) {
  return ::X509_NAME_dup(const_cast<X509_NAME*>(name));
}
#endif

} // namespace Envoy
