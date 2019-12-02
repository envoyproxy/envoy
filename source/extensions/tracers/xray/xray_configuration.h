#pragma once
#include <string>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * X-Ray configuration Model.
 */
struct XRayConfiguration {
  const std::string daemon_endpoint_;
  const std::string segment_name_;
  const std::string sampling_rules_;
};

enum class SamplingDecision {
  Sampled,
  NotSampled,
  Unknown,
};

/**
 * X-Ray header Model.
 */
struct XRayHeader {
  std::string trace_id_;
  std::string parent_id_;
  SamplingDecision sample_decision_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
