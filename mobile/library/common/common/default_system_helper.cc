#include "library/common/common/default_system_helper.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view /*hostname*/) { return true; }

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(const std::vector<std::string>& /*certs*/,
                                              absl::string_view /*hostname*/) {
  envoy_cert_validation_result result;
  // result.result = ENVOY_FAILURE;
  // result.tls_alert = 80; // internal error
  // result.error_details = "Certifcate verification not implemented ";
  result.result = ENVOY_SUCCESS;
  result.tls_alert = 0;
  result.error_details = "";
  return result;
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() {}

} // namespace Envoy
