#pragma once

#include "library/common/common/system_helper.h"

namespace Envoy {

/**
 * Default implementation of SystemHelper which invokes the appropriate
 * platform-specific system APIs.
 */
class DefaultSystemHelper : public SystemHelper {
public:
  ~DefaultSystemHelper() override = default;

  // SystemHelper:
  bool isCleartextPermitted(absl::string_view hostname) override;
  envoy_cert_validation_result validateCertificateChain(absl::Span<const absl::string_view> certs, absl::string_view hostname) override;
  void cleanupAfterCertificateValidation() override;
};

} // namespace Envoy
