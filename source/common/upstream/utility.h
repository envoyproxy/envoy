
#pragma once

#include <string>

#include "envoy/api/v2/cds.pb.h"

namespace Envoy {
namespace Upstream {
namespace Utility {

/**
 * Returns string representation of envoy::api::v2::Cluster::LbPolicy.
 */
const std::string lbPolicyToString(const envoy::api::v2::Cluster::LbPolicy& lb_policy);

/**
 * Returns string representation of envoy::api::v2::Cluster::DiscoveryType.
 */
const std::string
discoveryTypeToString(const envoy::api::v2::Cluster::DiscoveryType& discovery_type);

} // namespace Utility
} // namespace Upstream
} // namespace Envoy