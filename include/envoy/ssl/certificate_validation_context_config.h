#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

class CertificateValidationContextConfig {
public:
  virtual ~CertificateValidationContextConfig() {}

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
   * @return The subject alt names to be verified, if enabled. Otherwise, ""
   */
  virtual const std::vector<std::string>& verifySubjectAltNameList() const PURE;

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
};

typedef std::unique_ptr<CertificateValidationContextConfig> CertificateValidationContextConfigPtr;

} // namespace Ssl
} // namespace Envoy
