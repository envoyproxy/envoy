#include "library/common/system/default_system_helper.h"
#include "library/jni/android_jni_utility.h"
#include "library/jni/android_network_utility.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view hostname) {
  return JNI::isCleartextPermitted(hostname);
}

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(const std::vector<std::string>& certs,
                                              absl::string_view hostname) {
  return JNI::verifyX509CertChain(certs, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() { JNI::jvmDetachThread(); }

} // namespace Envoy
