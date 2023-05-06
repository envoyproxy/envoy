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


envoy_cert_validation_result verify_x509_cert_chain(const envoy_data* certs, uint8_t size,
                                                    const char* host_name);

void jvm_detach_thread();
