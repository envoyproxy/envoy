#pragma once

#include <shared_mutex>

#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

class ThreadAwareLoadBalancerBase : public LoadBalancerBase, public ThreadAwareLoadBalancer {
public:
  /**
   * Base class for a hashing load balancer implemented for use in a thread aware load balancer.
   * TODO(mattklein123): Currently only RingHash and Maglev use the thread aware load balancer.
   *                     The hash is pre-computed prior to getting to the real load balancer for
   *                     use in priority selection. In the future we likely we will want to pass
   *                     through the full load balancer context in case a future implementation
   *                     wants to use it.
   */
  class HashingLoadBalancer {
  public:
    virtual ~HashingLoadBalancer() {}
    virtual HostConstSharedPtr chooseHost(uint64_t hash) const PURE;
  };
  typedef std::shared_ptr<HashingLoadBalancer> HashingLoadBalancerSharedPtr;

  // Upstream::ThreadAwareLoadBalancer
  LoadBalancerFactorySharedPtr factory() override { return factory_; }
  void initialize() override;

protected:
  ThreadAwareLoadBalancerBase(const PrioritySet& priority_set, ClusterStats& stats,
                              Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                              const envoy::api::v2::Cluster::CommonLbConfig& common_config)
      : LoadBalancerBase(priority_set, stats, runtime, random, common_config),
        factory_(new LoadBalancerFactoryImpl(stats, random)) {}

private:
  struct PerPriorityState {
    std::shared_ptr<HashingLoadBalancer> current_lb_;
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

  virtual HashingLoadBalancerSharedPtr createLoadBalancer(const HostSet& host_set) PURE;
  void refresh();

  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
};

} // namespace Upstream
} // namespace Envoy
