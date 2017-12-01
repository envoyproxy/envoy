#pragma once

#include <cstdint>
#include <vector>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Upstream {

/**
 * A load balancer that implements consistent modulo hashing ("ketama"). Currently, zone aware
 * routing is not supported. A ring is kept for all hosts as well as a ring for healthy hosts.
 * Unless we are in panic mode, the healthy host ring is used.
 * In the future it would be nice to support:
 * 1) Weighting.
 * 2) Per-zone rings and optional zone aware routing (not all applications will want this).
 * 3) Max request fallback to support hot shards (not all applications will want this).
 */
class RingHashLoadBalancer : public LoadBalancer, Logger::Loggable<Logger::Id::upstream> {
public:
  RingHashLoadBalancer(HostSet& host_set, ClusterStats& stats, Runtime::Loader& runtime,
                       Runtime::RandomGenerator& random,
                       const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config);

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  struct RingEntry {
    uint64_t hash_;
    HostConstSharedPtr host_;
  };

  struct Ring {
    HostConstSharedPtr chooseHost(LoadBalancerContext* context, Runtime::RandomGenerator& random);
    void create(const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
                const std::vector<HostSharedPtr>& hosts);

    std::vector<RingEntry> ring_;
  };

  void refresh();

  HostSet& host_set_;
  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config_;
  Ring all_hosts_ring_;
  Ring healthy_hosts_ring_;
};

} // namespace Upstream
} // namespace Envoy
