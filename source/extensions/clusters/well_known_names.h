#pragma once

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {

/**
 * Well-known cluster types, this supersede the service discovery types
 */
class ClusterTypeValues {
public:
  // Static clusters (cluster that have a fixed number of hosts with resolved IP addresses).
  const std::string Static = "envoy.cluster.static";

  // Strict DNS (cluster that periodic DNS resolution and updates the host member set if the DNS
  // members change).
  const std::string StrictDns = "envoy.cluster.strict_dns";

  // Logical DNS (cluster that creates a single logical host that wraps an async DNS resolver).
  const std::string LogicalDns = "envoy.cluster.logical_dns";

  // Endpoint Discovery Service (dynamic cluster that reads host information from the Endpoint
  // Discovery Service).
  const std::string Eds = "envoy.cluster.eds";

  // Original destination (dynamic cluster that automatically adds hosts as needed based on the
  // original destination address of the downstream connection).
  const std::string OriginalDst = "envoy.cluster.original_dst";
};

using ClusterTypes = ConstSingleton<ClusterTypeValues>;

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
