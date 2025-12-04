#include "source/extensions/clusters/dynamic_modules/cluster_config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

DynamicModuleClusterConfig::DynamicModuleClusterConfig(
    const absl::string_view cluster_name, const absl::string_view cluster_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Server::Configuration::ServerFactoryContext& context)
    : cluster_name_(cluster_name), cluster_config_(cluster_config),
      dynamic_module_(std::move(dynamic_module)) {
  UNREFERENCED_PARAMETER(context);
}

DynamicModuleClusterConfig::~DynamicModuleClusterConfig() {
  if (in_module_config_ == nullptr) {
    return;
  }
  on_cluster_config_destroy_(in_module_config_);
  in_module_config_ = nullptr;
}

absl::StatusOr<DynamicModuleClusterConfigSharedPtr>
newDynamicModuleClusterConfig(const absl::string_view cluster_name,
                              const absl::string_view cluster_config,
                              Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                              Server::Configuration::ServerFactoryContext& context) {
  auto config = std::make_shared<DynamicModuleClusterConfig>(cluster_name, cluster_config,
                                                             std::move(dynamic_module), context);

  // Resolve the function pointers from the dynamic module.
  auto on_cluster_config_new =
      config->dynamic_module_
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_cluster_config_new)>(
              "envoy_dynamic_module_on_cluster_config_new");
  RETURN_IF_NOT_OK_REF(on_cluster_config_new.status());

  auto on_cluster_config_destroy =
      config->dynamic_module_->getFunctionPointer<OnClusterConfigDestroyType>(
          "envoy_dynamic_module_on_cluster_config_destroy");
  RETURN_IF_NOT_OK_REF(on_cluster_config_destroy.status());

  auto on_cluster_new = config->dynamic_module_->getFunctionPointer<OnClusterNewType>(
      "envoy_dynamic_module_on_cluster_new");
  RETURN_IF_NOT_OK_REF(on_cluster_new.status());

  auto on_cluster_destroy = config->dynamic_module_->getFunctionPointer<OnClusterDestroyType>(
      "envoy_dynamic_module_on_cluster_destroy");
  RETURN_IF_NOT_OK_REF(on_cluster_destroy.status());

  auto on_cluster_init = config->dynamic_module_->getFunctionPointer<OnClusterInitType>(
      "envoy_dynamic_module_on_cluster_init");
  RETURN_IF_NOT_OK_REF(on_cluster_init.status());

  auto on_cluster_cleanup = config->dynamic_module_->getFunctionPointer<OnClusterCleanupType>(
      "envoy_dynamic_module_on_cluster_cleanup");
  RETURN_IF_NOT_OK_REF(on_cluster_cleanup.status());

  auto on_load_balancer_new = config->dynamic_module_->getFunctionPointer<OnLoadBalancerNewType>(
      "envoy_dynamic_module_on_load_balancer_new");
  RETURN_IF_NOT_OK_REF(on_load_balancer_new.status());

  auto on_load_balancer_destroy =
      config->dynamic_module_->getFunctionPointer<OnLoadBalancerDestroyType>(
          "envoy_dynamic_module_on_load_balancer_destroy");
  RETURN_IF_NOT_OK_REF(on_load_balancer_destroy.status());

  auto on_load_balancer_choose_host =
      config->dynamic_module_->getFunctionPointer<OnLoadBalancerChooseHostType>(
          "envoy_dynamic_module_on_load_balancer_choose_host");
  RETURN_IF_NOT_OK_REF(on_load_balancer_choose_host.status());

  auto on_host_set_change = config->dynamic_module_->getFunctionPointer<OnHostSetChangeType>(
      "envoy_dynamic_module_on_host_set_change");
  RETURN_IF_NOT_OK_REF(on_host_set_change.status());

  auto on_host_health_change = config->dynamic_module_->getFunctionPointer<OnHostHealthChangeType>(
      "envoy_dynamic_module_on_host_health_change");
  RETURN_IF_NOT_OK_REF(on_host_health_change.status());

  // Store the function pointers.
  config->on_cluster_config_destroy_ = on_cluster_config_destroy.value();
  config->on_cluster_new_ = on_cluster_new.value();
  config->on_cluster_destroy_ = on_cluster_destroy.value();
  config->on_cluster_init_ = on_cluster_init.value();
  config->on_cluster_cleanup_ = on_cluster_cleanup.value();
  config->on_load_balancer_new_ = on_load_balancer_new.value();
  config->on_load_balancer_destroy_ = on_load_balancer_destroy.value();
  config->on_load_balancer_choose_host_ = on_load_balancer_choose_host.value();
  config->on_host_set_change_ = on_host_set_change.value();
  config->on_host_health_change_ = on_host_health_change.value();

  // Call the module's config creation function.
  config->in_module_config_ = on_cluster_config_new.value()(
      static_cast<envoy_dynamic_module_type_cluster_config_envoy_ptr>(config.get()),
      const_cast<char*>(cluster_name.data()), cluster_name.size(),
      const_cast<char*>(cluster_config.data()), cluster_config.size());

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to create in-module cluster config");
  }

  return config;
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
