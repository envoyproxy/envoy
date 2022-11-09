#pragma once

#include <string>

#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

using CommonLbConfigProto = envoy::config::cluster::v3::Cluster::CommonLbConfig;

/**
 * Utility functions for hosts.
 */
class HostUtility {
public:
  using HostStatusSet = std::bitset<32>;

  /**
   * Convert a host's health flags into a debug string.
   */
  static std::string healthFlagsToString(const Host& host);

  // A utility function to create override host status from lb config.
  static HostStatusSet createOverrideHostStatus(const CommonLbConfigProto& common_config);

  // A utility function to select override host from host map according to load balancer context.
  static HostConstSharedPtr selectOverrideHost(const HostMap* host_map, HostStatusSet status,
                                               LoadBalancerContext* context);
};

} // namespace Upstream
} // namespace Envoy
