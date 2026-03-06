#include "source/extensions/load_balancing_policies/dynamic_modules/config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

namespace {

/**
 * Thread-aware load balancer implementation that creates DynamicModuleLoadBalancer instances.
 */
class ThreadAwareLb : public Upstream::ThreadAwareLoadBalancer {
public:
  ThreadAwareLb(Upstream::LoadBalancerFactorySharedPtr factory) : factory_(std::move(factory)) {}

  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

private:
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

/**
 * Factory for creating worker-local DynamicModuleLoadBalancer instances.
 */
class LbFactory : public Upstream::LoadBalancerFactory {
public:
  LbFactory(DynamicModuleLbConfigSharedPtr config, const std::string& cluster_name)
      : config_(std::move(config)), cluster_name_(cluster_name) {}

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
    return std::make_unique<DynamicModuleLoadBalancer>(config_, params.priority_set, cluster_name_);
  }

  bool recreateOnHostChange() const override { return false; }

private:
  DynamicModuleLbConfigSharedPtr config_;
  const std::string cluster_name_;
};

} // namespace

Upstream::ThreadAwareLoadBalancerPtr
Factory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                const Upstream::ClusterInfo& cluster_info,
                const Upstream::PrioritySet& /*priority_set*/, Runtime::Loader&,
                Random::RandomGenerator& /*random*/, TimeSource& /*time_source*/) {
  const auto* typed_config = dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config.ptr());
  ASSERT(typed_config != nullptr, "Invalid dynamic module load balancer config");

  return std::make_unique<ThreadAwareLb>(
      std::make_shared<LbFactory>(typed_config->config(), cluster_info.name()));
}

absl::StatusOr<Upstream::LoadBalancerConfigPtr>
Factory::loadConfig(Server::Configuration::ServerFactoryContext&, const Protobuf::Message& config) {
  const auto& typed_config = dynamic_cast<const DynamicModulesLbProto&>(config);
  const auto& module_config = typed_config.dynamic_module_config();
  const std::string& module_name = module_config.name();

  // Load the dynamic module.
  auto module_or_error = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_name, module_config.do_not_close(), module_config.load_globally());
  if (!module_or_error.ok()) {
    return absl::InvalidArgumentError(fmt::format("failed to load dynamic module '{}': {}",
                                                  module_name, module_or_error.status().message()));
  }

  // Create the load balancer configuration.
  std::string config_bytes;
  if (typed_config.has_lb_policy_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(typed_config.lb_policy_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config_bytes = std::move(config_or_error.value());
  }
  auto lb_config_or_error = DynamicModuleLbConfig::create(
      typed_config.lb_policy_name(), config_bytes, std::move(module_or_error.value()));
  if (!lb_config_or_error.ok()) {
    return absl::InvalidArgumentError(
        fmt::format("failed to create load balancer config for module '{}': {}", module_name,
                    lb_config_or_error.status().message()));
  }

  return std::make_unique<TypedDynamicModuleLbConfig>(std::move(lb_config_or_error.value()));
}

REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
