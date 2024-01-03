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

/**
 * Helper class to hold either a legacy or production config.
 */
template <class ActiveType, class LegacyType> class ActiveOrLegacy {
public:
  template <class BaseType> static ActiveOrLegacy get(const BaseType* base) {
    auto* active_type = dynamic_cast<const ActiveType*>(base);
    if (active_type != nullptr) {
      return {active_type};
    }
    auto* legacy_type = dynamic_cast<const LegacyType*>(base);
    if (legacy_type != nullptr) {
      return {legacy_type};
    }

    return {};
  }

  bool hasActive() const { return active_ != nullptr; }
  bool hasLegacy() const { return legacy_ != nullptr; }

  const ActiveType* active() const {
    ASSERT(hasActive());
    return active_;
  }
  const LegacyType* legacy() const {
    ASSERT(hasLegacy());
    return legacy_;
  }

private:
  ActiveOrLegacy() = default;
  ActiveOrLegacy(const ActiveType* active) : active_(active) {}
  ActiveOrLegacy(const LegacyType* legacy) : legacy_(legacy) {}

  const ActiveType* active_{};
  const LegacyType* legacy_{};
};

} // namespace Common
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
