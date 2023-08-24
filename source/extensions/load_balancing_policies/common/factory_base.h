#pragma once

#include <memory>

#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Common {

template <class ProtoType, class Impl>
class FactoryBase : public Upstream::TypedLoadBalancerFactoryBase<ProtoType> {
public:
  FactoryBase(const std::string& name) : Upstream::TypedLoadBalancerFactoryBase<ProtoType>(name) {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Envoy::Random::RandomGenerator& random,
                                              TimeSource& time_source) override {

    return std::make_unique<ThreadAwareLb>(std::make_shared<LbFactory>(
        lb_config, cluster_info, priority_set, runtime, random, time_source));
  }

private:
  class LbFactory : public Upstream::LoadBalancerFactory {
  public:
    LbFactory(OptRef<const Upstream::LoadBalancerConfig> lb_config,
              const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& priority_set,
              Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random,
              TimeSource& time_source)
        : lb_config_(lb_config), cluster_info_(cluster_info), priority_set_(priority_set),
          runtime_(runtime), random_(random), time_source_(time_source) {}

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
      return Impl()(params, lb_config_, cluster_info_, priority_set_, runtime_, random_,
                    time_source_);
    }

    bool recreateOnHostChange() const override { return false; }

  public:
    OptRef<const Upstream::LoadBalancerConfig> lb_config_;

    const Upstream::ClusterInfo& cluster_info_;
    const Upstream::PrioritySet& priority_set_;
    Runtime::Loader& runtime_;
    Envoy::Random::RandomGenerator& random_;
    TimeSource& time_source_;
  };

  class ThreadAwareLb : public Upstream::ThreadAwareLoadBalancer {
  public:
    ThreadAwareLb(Upstream::LoadBalancerFactorySharedPtr factory) : factory_(std::move(factory)) {}

    Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
    void initialize() override {}

  private:
    Upstream::LoadBalancerFactorySharedPtr factory_;
  };

  const std::string name_;
};

} // namespace Common
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
