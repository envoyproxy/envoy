#pragma once
#include <string>

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

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
  const std::string origin_;
  const absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata_;
};

enum class SamplingDecision {
  Unknown, // default
  Sampled,
  NotSampled,
};

/**
 * X-Ray header Model.
 */
struct XRayHeader {
  std::string trace_id_;
  std::string parent_id_;
  SamplingDecision sample_decision_{};
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
