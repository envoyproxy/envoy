#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "library/common/api/c_types.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"
#include "library/common/jni/import/jni_import.h"

// NOLINT(namespace-envoy)

/* Calls up through JNI to validate given certificates.
 */
jobject call_jvm_verify_x509_cert_chain(JNIEnv* env, const std::vector<std::string>& cert_chain,
                                        std::string auth_type, absl::string_view hostname);

envoy_cert_validation_result verify_x509_cert_chain(absl::Span<const absl::string_view> certs,
                                                    absl::string_view hostname);

void jvm_detach_thread();
