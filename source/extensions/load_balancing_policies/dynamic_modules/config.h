#pragma once

#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/lb_config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

using DynamicModulesLbProto = envoy::extensions::load_balancing_policies::dynamic_modules::v3::
    DynamicModulesLoadBalancerConfig;

/**
 * Load balancer config that wraps the dynamic module configuration.
 */
class TypedDynamicModuleLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedDynamicModuleLbConfig(DynamicModuleLbConfigSharedPtr config) : config_(std::move(config)) {}

  const DynamicModuleLbConfigSharedPtr& config() const { return config_; }

private:
  DynamicModuleLbConfigSharedPtr config_;
};

/**
 * Factory for creating Dynamic Module load balancers.
 */
class Factory : public Upstream::TypedLoadBalancerFactoryBase<DynamicModulesLbProto>,
                public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.dynamic_modules") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override;
};

DECLARE_FACTORY(Factory);

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
