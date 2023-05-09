#include "library/common/common/default_system_helper.h"
#include "library/common/jni/android_jni_utility.h"
#include "library/common/jni/android_network_utility.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view hostname) {
  return is_cleartext_permitted(hostname);
}

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(const std::vector<std::string>& certs,
                                              absl::string_view hostname) {
  return verify_x509_cert_chain(certs, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() { jvm_detach_thread(); }

} // namespace Envoy
