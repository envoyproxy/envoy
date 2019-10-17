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
  const std::string daemon_endpoint;
  const std::string segment_name;
  const std::string sampling_rules;
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
  std::string trace_id;
  std::string parent_id;
  SamplingDecision sample_decision;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
