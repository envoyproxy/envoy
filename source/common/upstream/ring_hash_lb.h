#pragma once

#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/ring.h"
#include "source/common/upstream/thread_aware_lb_impl.h"

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
                             protected Logger::Loggable<Logger::Id::upstream> {
public:
  RingHashLoadBalancer(
      const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>& config,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  const RingStats& stats() const { return stats_; }

  using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;

private:
  Ring::HashFunction toRingHashFunction(const HashFunction&) const;

protected:
  using RingConstSharedPtr = std::shared_ptr<const Ring>;

  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                     double min_normalized_weight, double /* max_normalized_weight */) override {
    HashingLoadBalancerSharedPtr ring_hash_lb =
        std::make_shared<Ring>(normalized_host_weights, min_normalized_weight, min_ring_size_,
                               max_ring_size_, hash_function_, use_hostname_for_hashing_, stats_);
    if (hash_balance_factor_ == 0) {
      return ring_hash_lb;
    }

    return std::make_shared<BoundedLoadHashingLoadBalancer>(
        ring_hash_lb, std::move(normalized_host_weights), hash_balance_factor_);
  }

  Stats::ScopeSharedPtr scope_;
  // Ideally Ring should have its own stats but at the time of refactoring `Ring` into its own
  // library, we don't intend to change the ownership model as it will change the stats interface to
  // the load balancer.
  // TODO (jojy): Reconsider the stats interface.
  RingStats stats_;

  const uint64_t min_ring_size_;
  const uint64_t max_ring_size_;
  const Ring::HashFunction hash_function_;
  const bool use_hostname_for_hashing_;
  const uint32_t hash_balance_factor_;
};

} // namespace Upstream
} // namespace Envoy
