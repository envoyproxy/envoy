#pragma once
#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

struct XRayConfiguration {
  XRayConfiguration(absl::string_view daemon_endpoint, absl::string_view segment_name,
                    absl::string_view sampling_rules)
      : daemon_endpoint_(daemon_endpoint), segment_name_(segment_name),
        sampling_rules_(sampling_rules) {}

  const std::string daemon_endpoint_;
  const std::string segment_name_;
  const std::string sampling_rules_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
