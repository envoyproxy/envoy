#include "library/common/network/apple_platform_cert_verifier.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecCertificate.h>
#include <Security/SecPolicy.h>
#include <Security/SecTrust.h>

#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"
#include "openssl/ssl.h"

// NOLINT(namespace-envoy)

// Returns a new CFMutableArrayRef containing a series of SecPolicyRefs to be
// added to a SecTrustRef used to validate a certificate for an SSL server,
// or NULL on failure.
CFMutableArrayRef CreateTrustPolicies() {
  CFMutableArrayRef policies = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
  if (!policies) {
    return NULL;
  }

  SecPolicyRef ssl_policy = SecPolicyCreateBasicX509();
  CFArrayAppendValue(policies, ssl_policy);
  CFRelease(ssl_policy);

  ssl_policy = SecPolicyCreateSSL(true, NULL);
  CFArrayAppendValue(policies, ssl_policy);
  CFRelease(ssl_policy);

  return policies;
}

// Returns a new CFMutableArrayRef containing the specified certificates
// in the form expected by Security.framework and Keychain Services, or
// NULL on failure.
CFMutableArrayRef CreateSecCertificateArray(const std::vector<std::string>& certs) {
  CFMutableArrayRef cert_array =
      CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);

  if (!cert_array) {
    return NULL;
  }

  for (absl::string_view cert : certs) {
    CFDataRef cert_data = CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const uint8_t*>(cert.data()), cert.length());
    if (!cert_data) {
      CFRelease(cert_array);
      return NULL;
    }
    SecCertificateRef sec_cert = SecCertificateCreateWithData(NULL, cert_data);
    if (!sec_cert) {
      CFRelease(cert_array);
      return NULL;
    }
    CFArrayAppendValue(cert_array, sec_cert);
    CFRelease(cert_data);
  }
  return cert_array;
}

// Helper to create a envoy_cert_validation_result.
envoy_cert_validation_result make_result(envoy_status_t status, uint8_t tls_alert,
                                         const char* error_details) {
  envoy_cert_validation_result result;
  result.result = status;
  result.tls_alert = tls_alert;
  result.error_details = error_details;
  return result;
}

envoy_cert_validation_result verify_cert(const std::vector<std::string>& certs,
                                         absl::string_view /*hostname*/) {
  CFArrayRef trust_policies = CreateTrustPolicies();
  if (!trust_policies) {
    return make_result(ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
                       "validation couldn't be conducted.");
  }

  CFMutableArrayRef cert_array = CreateSecCertificateArray(certs);
  if (!cert_array) {
    return make_result(ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
                       "validation couldn't be conducted.");
  }

  SecTrustRef trust = NULL;
  OSStatus status = SecTrustCreateWithCertificates(cert_array, trust_policies, &trust);
  if (status) {
    return make_result(ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
                       "validation couldn't be conducted.");
  }

  CFErrorRef error;
  bool verified = SecTrustEvaluateWithError(trust, &error);

  CFRelease(cert_array);
  CFRelease(trust);

  if (!verified) {
    return make_result(ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
                       "validation couldn't be conducted.");
  }
  return make_result(ENVOY_SUCCESS, 0, "");
}
