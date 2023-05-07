#pragma once

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"

// NOLINT(namespace-envoy)
envoy_cert_validation_result verify_cert(absl::Span<const absl::string_view> certs, absl::string_view hostname);
