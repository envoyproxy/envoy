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

  // Redis cluster (cluster that reads host information using the redis cluster protocol).
  const std::string Redis = "envoy.clusters.redis";

  // Dynamic forward proxy cluster. This cluster is designed to work directly with the
  // dynamic forward proxy HTTP filter.
  const std::string DynamicForwardProxy = "envoy.clusters.dynamic_forward_proxy";

  // Aggregate cluster which may contain different types of clusters. It allows load balance between
  // different type of clusters.
  const std::string Aggregate = "envoy.clusters.aggregate";
};

using ClusterTypes = ConstSingleton<ClusterTypeValues>;

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
