#pragma once

#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"

// NOLINT(namespace-envoy)
envoy_cert_validation_result verify_cert(const envoy_data* certs, uint8_t num_certs,
                                         const char* hostname);
