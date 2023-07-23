#include "source/extensions/load_balancing_policies/subset/config.h"

#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {

Upstream::LoadBalancerPtr Factory::create(const Upstream::ClusterInfo& cluster,
                                          const Upstream::PrioritySet& priority_set,
                                          const Upstream::PrioritySet* local_priority_set,
                                          Runtime::Loader& runtime, Random::RandomGenerator& random,
                                          TimeSource& time_source) {
  auto child_lb_creator = std::make_unique<Upstream::LegacyChildLoadBalancerCreatorImpl>(
      cluster.lbType(), cluster.lbRingHashConfig(), cluster.lbMaglevConfig(),
      cluster.lbRoundRobinConfig(), cluster.lbLeastRequestConfig(), cluster.lbConfig());

  return std::make_unique<Upstream::SubsetLoadBalancer>(
      cluster.lbSubsetInfo(), std::move(child_lb_creator), priority_set, local_priority_set,
      cluster.lbStats(), cluster.statsScope(), runtime, random, time_source);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::NonThreadAwareLoadBalancerFactory);

using LoadBalancerSubsetInfoImpl =
    Upstream::LoadBalancerSubsetInfoImplBase<Upstream::SubsetLoadbalancingPolicyProto>;

class SubsetLoadBalancerConfig : public Upstream::LoadBalancerConfig {
public:
  SubsetLoadBalancerConfig(const Upstream::SubsetLoadbalancingPolicyProto& subset_config,
                           ProtobufMessage::ValidationVisitor& visitor)
      : subset_info_(subset_config) {

    absl::InlinedVector<absl::string_view, 4> missing_policies;

    for (const auto& policy : subset_config.subset_lb_policy().policies()) {
      auto* factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(
          policy.typed_extension_config(), /*is_optional=*/true);

      if (factory != nullptr) {
        // Load and validate the configuration.
        auto sub_lb_proto_message = factory->createEmptyConfigProto();
        Config::Utility::translateOpaqueConfig(policy.typed_extension_config().typed_config(),
                                               visitor, *sub_lb_proto_message);

        sub_load_balancer_config_ = factory->loadConfig(std::move(sub_lb_proto_message), visitor);
        sub_load_balancer_factory_ = factory;
        break;
      }

      missing_policies.push_back(policy.typed_extension_config().name());
    }

    if (sub_load_balancer_factory_ == nullptr) {
      throw EnvoyException(fmt::format("cluster: didn't find a registered load balancer factory "
                                       "implementation for subset lb with names from [{}]",
                                       absl::StrJoin(missing_policies, ", ")));
    }
  }

  Upstream::ThreadAwareLoadBalancerPtr
  createLoadBalancer(const Upstream::ClusterInfo& cluster_info,
                     const Upstream::PrioritySet& child_priority_set, Runtime::Loader& runtime,
                     Random::RandomGenerator& random, TimeSource& time_source) const {
    return sub_load_balancer_factory_->create(*sub_load_balancer_config_, cluster_info,
                                              child_priority_set, runtime, random, time_source);
  }

  const Upstream::LoadBalancerSubsetInfo& subsetInfo() const { return subset_info_; }

private:
  LoadBalancerSubsetInfoImpl subset_info_;

  Upstream::LoadBalancerConfigPtr sub_load_balancer_config_;
  Upstream::TypedLoadBalancerFactory* sub_load_balancer_factory_{};
};

class ChildLoadBalancerCreatorImpl : public Upstream::ChildLoadBalancerCreator {
public:
  ChildLoadBalancerCreatorImpl(const SubsetLoadBalancerConfig& subset_config,
                               const Upstream::ClusterInfo& cluster_info)
      : subset_config_(subset_config), cluster_info_(cluster_info) {}

  std::pair<Upstream::ThreadAwareLoadBalancerPtr, Upstream::LoadBalancerPtr>
  createLoadBalancer(const Upstream::PrioritySet& child_priority_set, const Upstream::PrioritySet*,
                     Upstream::ClusterLbStats&, Stats::Scope&, Runtime::Loader& runtime,
                     Random::RandomGenerator& random, TimeSource& time_source) override {
    return {subset_config_.createLoadBalancer(cluster_info_, child_priority_set, runtime, random,
                                              time_source),
            nullptr};
  }

private:
  const SubsetLoadBalancerConfig& subset_config_;
  const Upstream::ClusterInfo& cluster_info_;
};

class LbFactory : public Upstream::LoadBalancerFactory {
public:
  LbFactory(const SubsetLoadBalancerConfig& subset_config,
            const Upstream::ClusterInfo& cluster_info, Runtime::Loader& runtime,
            Random::RandomGenerator& random, TimeSource& time_source)
      : subset_config_(subset_config), cluster_info_(cluster_info), runtime_(runtime),
        random_(random), time_source_(time_source) {}

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
    auto child_lb_creator =
        std::make_unique<ChildLoadBalancerCreatorImpl>(subset_config_, cluster_info_);

    return std::make_unique<Upstream::SubsetLoadBalancer>(
        subset_config_.subsetInfo(), std::move(child_lb_creator), params.priority_set,
        params.local_priority_set, cluster_info_.lbStats(), cluster_info_.statsScope(), runtime_,
        random_, time_source_);
  }
  bool recreateOnHostChange() const override { return false; }

private:
  const SubsetLoadBalancerConfig& subset_config_;
  const Upstream::ClusterInfo& cluster_info_;

  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
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

Upstream::ThreadAwareLoadBalancerPtr
SubsetLbFactory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                        const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
                        Runtime::Loader& runtime, Random::RandomGenerator& random,
                        TimeSource& time_source) {

  const auto* typed_config = dynamic_cast<const SubsetLoadBalancerConfig*>(lb_config.ptr());
  // The load balancing policy configuration will be loaded and validated in the main thread when we
  // load the cluster configuration. So we can assume the configuration is valid here.
  ASSERT(typed_config != nullptr,
         "Invalid load balancing policy configuration for subset load balancer");

  // Create the load balancer factory that will be used to create the load balancer in the workers.
  auto lb_factory =
      std::make_shared<LbFactory>(*typed_config, cluster_info, runtime, random, time_source);

  // Move and store the load balancer factory in the thread aware load balancer. This thread aware
  // load balancer is simply a wrapper of the load balancer factory for subset lb and no actual
  // logic is implemented.
  return std::make_unique<ThreadAwareLb>(std::move(lb_factory));
}

Upstream::LoadBalancerConfigPtr
SubsetLbFactory::loadConfig(ProtobufTypes::MessagePtr config,
                            ProtobufMessage::ValidationVisitor& visitor) {
  ASSERT(config != nullptr);
  auto* proto_config = dynamic_cast<Upstream::SubsetLoadbalancingPolicyProto*>(config.get());

  // Load the subset load balancer configuration. This will contains child load balancer
  // config and child load balancer factory.
  return std::make_unique<SubsetLoadBalancerConfig>(*proto_config, visitor);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(SubsetLbFactory, Upstream::TypedLoadBalancerFactory);

} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
