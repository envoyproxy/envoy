#pragma once

#include <string>
#include <vector>

#include "library/common/api/c_types.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"
#include "library/common/jni/import/jni_import.h"

// NOLINT(namespace-envoy)

/* Calls up through JNI to validate given certificates.
 */
jobject call_jvm_verify_x509_cert_chain(JNIEnv* env, const std::vector<std::string>& cert_chain,
                                        std::string auth_type, std::string host);

/* Returns a group of C functions to do certificates validation using AndroidNetworkLibrary.
 */
envoy_cert_validator* get_android_cert_validator_api();

static constexpr const char* cert_validator_name = "platform_cert_validator";
