#pragma once

#include <vector>

#include "envoy/runtime/runtime.h"

#include "common/common/logger.h"
#include "common/upstream/thread_aware_lb_impl.h"
#include "common/upstream/upstream_impl.h"

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
  RingHashLoadBalancer(const PrioritySet& priority_set, ClusterStats& stats,
                       Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                       const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
                       const envoy::api::v2::Cluster::CommonLbConfig& common_config);

private:
  struct RingEntry {
    uint64_t hash_;
    HostConstSharedPtr host_;
  };

  struct Ring : public HashingLoadBalancer {
    Ring(const HostsPerLocality& hosts_per_locality,
         const LocalityWeightsConstSharedPtr& locality_weights,
         const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config);

    // ThreadAwareLoadBalancerBase::HashingLoadBalancer
    HostConstSharedPtr chooseHost(uint64_t hash) const override;

    std::vector<RingEntry> ring_;

    static const uint64_t DEFAULT_MIN_RING_SIZE = 1024;
    static const uint64_t DEFAULT_MAX_RING_SIZE = 1024 * 1024 * 8;
    static const uint64_t DEFAULT_TARGET_HASHES_PER_HOST = 256;
  };
  typedef std::shared_ptr<const Ring> RingConstSharedPtr;

  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr createLoadBalancer(const HostSet& host_set, bool in_panic) override {
    // TODO(mergeconflict): refactor this and MaglevLoadBalancer::createLoadBalancer back into base

    // Note that we only compute global panic on host set refresh. Given that the runtime setting
    // will rarely change, this is a reasonable compromise to avoid creating extra LBs when we only
    // need to create one per priority level.
    const bool has_locality =
        host_set.localityWeights() != nullptr && !host_set.localityWeights()->empty();
    if (in_panic) {
      if (!has_locality) {
        return std::make_shared<Ring>(HostsPerLocalityImpl(host_set.hosts(), false), nullptr,
                                      config_);
      } else {
        return std::make_shared<Ring>(host_set.hostsPerLocality(), host_set.localityWeights(),
                                      config_);
      }
    } else {
      if (!has_locality) {
        return std::make_shared<Ring>(HostsPerLocalityImpl(host_set.healthyHosts(), false), nullptr,
                                      config_);
      } else {
        return std::make_shared<Ring>(host_set.healthyHostsPerLocality(),
                                      host_set.localityWeights(), config_);
      }
    }
  }

  const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config_;
};

} // namespace Upstream
} // namespace Envoy
