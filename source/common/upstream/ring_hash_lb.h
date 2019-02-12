#pragma once

#include <vector>

#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"
#include "common/upstream/thread_aware_lb_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * All ring hash load balancer stats. @see stats_macros.h
 */
// clang-format off
#define ALL_RING_HASH_LOAD_BALANCER_STATS(GAUGE)                                                   \
  GAUGE(size)                                                                                 \
  GAUGE(replication_factor)
// clang-format on

/**
 * Struct definition for all ring hash load balancer stats. @see stats_macros.h
 */
struct RingHashLoadBalancerStats {
  ALL_RING_HASH_LOAD_BALANCER_STATS(GENERATE_GAUGE_STRUCT)
};

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
  RingHashLoadBalancer(const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
                       Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                       const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
                       const envoy::api::v2::Cluster::CommonLbConfig& common_config);

  const RingHashLoadBalancerStats& stats() const { return stats_; }

private:
  struct RingEntry {
    uint64_t hash_;
    HostConstSharedPtr host_;
  };

  struct Ring : public HashingLoadBalancer {
    Ring(const HostsPerLocality& hosts_per_locality,
         const LocalityWeightsConstSharedPtr& locality_weights,
         const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
         RingHashLoadBalancerStats& stats);

    // ThreadAwareLoadBalancerBase::HashingLoadBalancer
    HostConstSharedPtr chooseHost(uint64_t hash) const override;

    std::vector<RingEntry> ring_;

    static const uint64_t DefaultMinRingSize = 1024;
    static const uint64_t DefaultMaxRingSize = 1024 * 1024 * 8;

    RingHashLoadBalancerStats& stats_;
  };
  typedef std::shared_ptr<const Ring> RingConstSharedPtr;

  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr createLoadBalancer(const HostSet& host_set, bool in_panic) override {
    // Note that we only compute global panic on host set refresh. Given that the runtime setting
    // will rarely change, this is a reasonable compromise to avoid creating extra LBs when we only
    // need to create one per priority level.
    const bool has_locality =
        host_set.localityWeights() != nullptr && !host_set.localityWeights()->empty();
    if (in_panic) {
      if (!has_locality) {
        return std::make_shared<Ring>(HostsPerLocalityImpl(host_set.hosts(), false), nullptr,
                                      config_, stats_);
      } else {
        return std::make_shared<Ring>(host_set.hostsPerLocality(), host_set.localityWeights(),
                                      config_, stats_);
      }
    } else {
      if (!has_locality) {
        return std::make_shared<Ring>(HostsPerLocalityImpl(host_set.healthyHosts(), false), nullptr,
                                      config_, stats_);
      } else {
        return std::make_shared<Ring>(host_set.healthyHostsPerLocality(),
                                      host_set.localityWeights(), config_, stats_);
      }
    }
  }

  static RingHashLoadBalancerStats generateStats(Stats::Scope& scope);

  const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>& config_;
  Stats::ScopePtr scope_;
  RingHashLoadBalancerStats stats_;
};

} // namespace Upstream
} // namespace Envoy
