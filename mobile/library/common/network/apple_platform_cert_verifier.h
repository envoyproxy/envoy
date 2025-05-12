#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"

// NOLINT(namespace-envoy)
envoy_cert_validation_result verify_cert(const std::vector<std::string>& certs,
                                         absl::string_view hostname);
