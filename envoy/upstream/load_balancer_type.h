#pragma once

#include <set>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

/**
 * Type of load balancing to perform.
 * TODO(wbpcode): should we remove this enum because it is not used in the codebase?
 */
enum class LoadBalancerType : uint8_t {
  RoundRobin,
  LeastRequest,
  Random,
  RingHash,
  OriginalDst,
  Maglev,
  ClusterProvided,
  LoadBalancingPolicyConfig
};

} // namespace Upstream
} // namespace Envoy
