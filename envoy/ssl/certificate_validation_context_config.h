#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Ssl {

// SECURITY NOTE
//
// When adding or changing this interface, it is likely that a change is needed to
// `DefaultCertValidator::updateDigestForSessionId` in
// `source/common/tls/cert_validator/default_validator.cc`.
class CertificateValidationContextConfig {
public:
  virtual ~CertificateValidationContextConfig() = default;

  /**
   * @return The CA certificate to use for peer validation.
   */
  virtual const std::string& caCert() const PURE;

  /**
   * @return Path of the CA certificate to use for peer validation or "<inline>"
   * if the CA certificate was inlined.
   */
  virtual const std::string& caCertPath() const PURE;

  /**
   * @return The CRL to check if a cert is revoked.
   */
  virtual const std::string& certificateRevocationList() const PURE;

  /**
   * @return Path of the certificate revocation list, or "<inline>" if the CRL
   * was inlined.
   */
  virtual const std::string& certificateRevocationListPath() const PURE;

  /**
   * @return The subject alt name matchers to be verified, if enabled.
   */
  virtual const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>&
  subjectAltNameMatchers() const PURE;

  /**
   * @return A list of a hex-encoded SHA-256 certificate hashes to be verified.
   */
  virtual const std::vector<std::string>& verifyCertificateHashList() const PURE;

  /**
   * @return A list of a hex-encoded SHA-256 SPKI hashes to be verified.
   */
  virtual const std::vector<std::string>& verifyCertificateSpkiList() const PURE;

  /**
   * @return whether to ignore expired certificates (both too new and too old).
   */
  virtual bool allowExpiredCertificate() const PURE;

  /**
   * @return client certificate validation configuration.
   */
  virtual envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
      TrustChainVerification
      trustChainVerification() const PURE;

  /**
   * @return the configuration for the custom certificate validator if configured.
   */
  virtual const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&
  customValidatorConfig() const PURE;

  /**
   * @return a reference to the api object.
   */
  virtual Api::Api& api() const PURE;

  /**
   * @return whether to validate certificate chain with all CRL or not.
   */
  virtual bool onlyVerifyLeafCertificateCrl() const PURE;

  /**
   * @return the max depth used when verifying the certificate-chain
   */
  virtual absl::optional<uint32_t> maxVerifyDepth() const PURE;
};

using CertificateValidationContextConfigPtr = std::unique_ptr<CertificateValidationContextConfig>;

} // namespace Ssl
} // namespace Envoy
