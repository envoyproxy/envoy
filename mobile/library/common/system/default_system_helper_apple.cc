#include "library/common/network/apple_platform_cert_verifier.h"
#include "library/common/system/default_system_helper.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view /*hostname*/) { return true; }

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(const std::vector<std::string>& certs,
                                              absl::string_view hostname) {
  return verify_cert(certs, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() {}

} // namespace Envoy
