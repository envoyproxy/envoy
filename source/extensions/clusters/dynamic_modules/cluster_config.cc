#include "source/extensions/clusters/dynamic_modules/cluster_config.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

absl::StatusOr<DynamicModuleClusterConfigSharedPtr>
DynamicModuleClusterConfig::newDynamicModuleClusterConfig(
    const std::string& cluster_name, const std::string& cluster_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr module) {
  std::shared_ptr<DynamicModuleClusterConfig> config(
      new DynamicModuleClusterConfig(cluster_name, cluster_config, std::move(module)));

  // Resolve all required function pointers from the dynamic module.
#define RESOLVE_SYMBOL(name, type, member)                                                         \
  {                                                                                                \
    auto symbol_or_error = config->dynamic_module_->getFunctionPointer<type>(name);                \
    if (!symbol_or_error.ok()) {                                                                   \
      return symbol_or_error.status();                                                             \
    }                                                                                              \
    config->member = symbol_or_error.value();                                                      \
  }

  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_config_new", OnClusterConfigNewType,
                 on_cluster_config_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_config_destroy", OnClusterConfigDestroyType,
                 on_cluster_config_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_new", OnClusterNewType, on_cluster_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_destroy", OnClusterDestroyType,
                 on_cluster_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_init", OnClusterInitType, on_cluster_init_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_cleanup", OnClusterCleanupType,
                 on_cluster_cleanup_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_load_balancer_new", OnLoadBalancerNewType,
                 on_load_balancer_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_load_balancer_destroy", OnLoadBalancerDestroyType,
                 on_load_balancer_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_load_balancer_choose_host", OnLoadBalancerChooseHostType,
                 on_load_balancer_choose_host_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_host_set_change", OnHostSetChangeType,
                 on_host_set_change_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_host_health_change", OnHostHealthChangeType,
                 on_host_health_change_);

#undef RESOLVE_SYMBOL

  // Now call on_cluster_config_new to get the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {config->cluster_name_.data(),
                                                        config->cluster_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {config->cluster_config_.data(),
                                                          config->cluster_config_.size()};

  config->in_module_config_ =
      config->on_cluster_config_new_(config.get(), name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to create in-module cluster configuration");
  }

  return config;
}

DynamicModuleClusterConfig::DynamicModuleClusterConfig(
    const std::string& cluster_name, const std::string& cluster_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : in_module_config_(nullptr), cluster_name_(cluster_name), cluster_config_(cluster_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleClusterConfig::~DynamicModuleClusterConfig() {
  if (in_module_config_ != nullptr && on_cluster_config_destroy_ != nullptr) {
    on_cluster_config_destroy_(in_module_config_);
  }
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
