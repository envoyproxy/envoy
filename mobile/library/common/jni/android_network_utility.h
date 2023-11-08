#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "library/common/api/c_types.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"
#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_helper.h"

// NOLINT(namespace-envoy)

/* Calls up through JNI to validate given certificates.
 */
Envoy::JNI::LocalRefUniquePtr<jobject>
call_jvm_verify_x509_cert_chain(Envoy::JNI::JniHelper& jni_helper,
                                const std::vector<std::string>& cert_chain, std::string auth_type,
                                absl::string_view hostname);

envoy_cert_validation_result verify_x509_cert_chain(const std::vector<std::string>& certs,
                                                    absl::string_view hostname);

void jvm_detach_thread();
