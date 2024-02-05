#include "source/extensions/load_balancing_policies/subset/config.h"

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {

using SubsetLbProto = envoy::extensions::load_balancing_policies::subset::v3::Subset;
using ClusterProto = envoy::config::cluster::v3::Cluster;

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

class ChildLoadBalancerCreatorImpl : public Upstream::ChildLoadBalancerCreator {
public:
  ChildLoadBalancerCreatorImpl(const Upstream::SubsetLoadBalancerConfig& subset_config,
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
  const Upstream::SubsetLoadBalancerConfig& subset_config_;
  const Upstream::ClusterInfo& cluster_info_;
};

class LbFactory : public Upstream::LoadBalancerFactory {
public:
  LbFactory(const Upstream::SubsetLoadBalancerConfig& subset_config,
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
  const Upstream::SubsetLoadBalancerConfig& subset_config_;
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

  const auto* typed_config =
      dynamic_cast<const Upstream::SubsetLoadBalancerConfig*>(lb_config.ptr());
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
SubsetLbFactory::loadConfig(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& visitor) {

  auto active_or_legacy = Common::ActiveOrLegacy<SubsetLbProto, ClusterProto>::get(&config);
  ASSERT(active_or_legacy.hasLegacy() || active_or_legacy.hasActive());

  if (active_or_legacy.hasLegacy()) {
    if (active_or_legacy.legacy()->lb_policy() ==
        envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
      throw EnvoyException(
          fmt::format("cluster: LB policy {} cannot be combined with lb_subset_config",
                      envoy::config::cluster::v3::Cluster::LbPolicy_Name(
                          active_or_legacy.legacy()->lb_policy())));
    }

    auto sub_lb_pair =
        Upstream::LegacyLbPolicyConfigHelper::getTypedLbConfigFromLegacyProtoWithoutSubset(
            *active_or_legacy.legacy(), visitor);

    if (!sub_lb_pair.ok()) {
      throw EnvoyException(std::string(sub_lb_pair.status().message()));
    }

    return std::make_unique<Upstream::SubsetLoadBalancerConfig>(
        active_or_legacy.legacy()->lb_subset_config(), std::move(sub_lb_pair->config),
        sub_lb_pair->factory);
  }

  // Load the subset load balancer configuration. This will contains child load balancer
  // config and child load balancer factory.
  return std::make_unique<Upstream::SubsetLoadBalancerConfig>(*active_or_legacy.active(), visitor);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(SubsetLbFactory, Upstream::TypedLoadBalancerFactory);

} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
