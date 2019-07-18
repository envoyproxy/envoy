#include "common/upstream/utility.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Upstream {
namespace Utility {
    
const std::string lbPolicyToString(const envoy::api::v2::Cluster::LbPolicy& lb_policy) {
  switch (lb_policy) {
  case envoy::api::v2::Cluster::ROUND_ROBIN:
    return "ROUND_ROBIN";
  case envoy::api::v2::Cluster::LEAST_REQUEST:
    return "LEAST_REQUEST";
  case envoy::api::v2::Cluster::RANDOM:
    return "RANDOM";
  case envoy::api::v2::Cluster::RING_HASH:
    return "RING_HASH";
  case envoy::api::v2::Cluster::ORIGINAL_DST_LB:
    return "ORIGINAL_DST_LB";
  case envoy::api::v2::Cluster::MAGLEV:
    return "MAGLEV";
  case envoy::api::v2::Cluster::CLUSTER_PROVIDED:
    return "CLUSTER_PROVIDED";
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

const std::string
discoveryTypeToString(const envoy::api::v2::Cluster::DiscoveryType& discovery_type) {
  switch (discovery_type) {
  case envoy::api::v2::Cluster::STATIC:
    return "STATIC";
  case envoy::api::v2::Cluster::STRICT_DNS:
    return "STRICT_DNS";
  case envoy::api::v2::Cluster::LOGICAL_DNS:
    return "LOGICAL_DNS";
  case envoy::api::v2::Cluster::EDS:
    return "EDS";
  case envoy::api::v2::Cluster::ORIGINAL_DST:
    return "ORIGINAL_DST";
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Utility
} // namespace Upstream
} // namespace Envoy