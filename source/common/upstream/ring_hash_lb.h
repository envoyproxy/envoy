#pragma once

#include <cstdint>
#include <shared_mutex>
#include <vector>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/logger.h"
#include "common/upstream/load_balancer_impl.h"

#include "absl/base/thread_annotations.h"

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
class RingHashLoadBalancer : public LoadBalancerBase,
                             public ThreadAwareLoadBalancer,
                             Logger::Loggable<Logger::Id::upstream> {
public:
  RingHashLoadBalancer(PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
                       Runtime::RandomGenerator& random,
                       const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config);

  // Upstream::ThreadAwareLoadBalancer
  LoadBalancerFactorySharedPtr factory() override { return factory_; }
  void initialize() override;

private:
  struct RingEntry {
    uint64_t hash_;
    HostConstSharedPtr host_;
  };

  struct Ring {
    Ring(const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config,
         const std::vector<HostSharedPtr>& hosts);
    HostConstSharedPtr chooseHost(uint64_t hash) const;

    std::vector<RingEntry> ring_;
  };
  typedef std::shared_ptr<const Ring> RingConstSharedPtr;

  struct PerPriorityState {
    RingConstSharedPtr current_ring_;
    bool global_panic_{};
  };
  typedef std::unique_ptr<PerPriorityState> PerPriorityStatePtr;

  struct LoadBalancerImpl : public LoadBalancer {
    LoadBalancerImpl(ClusterStats& stats, Runtime::RandomGenerator& random)
        : stats_(stats), random_(random) {}

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

    ClusterStats& stats_;
    Runtime::RandomGenerator& random_;
    std::shared_ptr<std::vector<PerPriorityStatePtr>> per_priority_state_;
    std::shared_ptr<std::vector<uint32_t>> per_priority_load_;
  };

  struct LoadBalancerFactoryImpl : public LoadBalancerFactory {
    LoadBalancerFactoryImpl(ClusterStats& stats, Runtime::RandomGenerator& random)
        : stats_(stats), random_(random) {}

    // Upstream::LoadBalancerFactory
    LoadBalancerPtr create() override;

    ClusterStats& stats_;
    Runtime::RandomGenerator& random_;
    std::shared_timed_mutex mutex_;
    // TOOD(mattklein123): Added GUARDED_BY(mutex_) to to the following variables. OSX clang
    // seems to not like them with shared mutexes so we need to ifdef them out on OSX. I don't
    // have time to do this right now.
    std::shared_ptr<std::vector<PerPriorityStatePtr>> per_priority_state_;
    // This is split out of PerPriorityState so LoadBalancerBase::ChoosePriorirty can be reused.
    std::shared_ptr<std::vector<uint32_t>> per_priority_load_;
  };

  void refresh();

  const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& config_;
  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
};

} // namespace Upstream
} // namespace Envoy
