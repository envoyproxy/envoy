#include "library/common/common/default_system_helper.h"
#include "library/common/network/apple_platform_cert_verifier.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view /*hostname*/) { return true; }

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(absl::Span<const absl::string_view> certs,
                                              absl::string_view hostname) {
  return verify_cert(certs, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() {}

} // namespace Envoy
