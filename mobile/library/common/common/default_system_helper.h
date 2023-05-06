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
  envoy_cert_validation_result validateCertificateChain(const envoy_data* certs, uint8_t size,
                                                        const char* host_name) override;
  void cleanupAfterCertificateValidation() override;
};

} // namespace Envoy
