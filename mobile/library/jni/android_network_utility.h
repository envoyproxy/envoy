#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"
#include "library/jni/jni_helper.h"

namespace Envoy {
namespace JNI {

/**
 * Calls up through JNI to validate given certificates.
 */
LocalRefUniquePtr<jobject> callJvmVerifyX509CertChain(JniHelper& jni_helper,
                                                      const std::vector<std::string>& cert_chain,
                                                      std::string auth_type,
                                                      absl::string_view hostname);

envoy_cert_validation_result verifyX509CertChain(const std::vector<std::string>& certs,
                                                 absl::string_view hostname);

void jvmDetachThread();

} // namespace JNI
} // namespace Envoy
