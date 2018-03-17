#pragma once

#include <vector>

#include "envoy/runtime/runtime.h"

#include "common/common/logger.h"
#include "common/upstream/thread_aware_lb_impl.h"

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
class RingHashLoadBalancer : public ThreadAwareLoadBalancerBase,
                             Logger::Loggable<Logger::Id::upstream> {
public:
  RingHashLoadBalancer(PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
                       Runtime::RandomGenerator& random,
                       const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
                       const envoy::api::v2::Cluster::CommonLbConfig& common_config);

private:
  struct RingEntry {
    uint64_t hash_;
    HostConstSharedPtr host_;
  };

  struct Ring : public HashingLoadBalancer {
    Ring(const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
         const HostVector& hosts);

    // ThreadAwareLoadBalancerBase::HashingLoadBalancer
    HostConstSharedPtr chooseHost(uint64_t hash) const override;

    std::vector<RingEntry> ring_;
  };
  typedef std::shared_ptr<const Ring> RingConstSharedPtr;

  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr createLoadBalancer(const HostVector& hosts) override {
    return std::make_shared<Ring>(config_, hosts);
  }

  const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config_;
};

} // namespace Upstream
} // namespace Envoy
