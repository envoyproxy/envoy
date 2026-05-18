#include "source/extensions/load_balancing_policies/dynamic_modules/lb_config.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

absl::StatusOr<DynamicModuleLbConfigSharedPtr>
DynamicModuleLbConfig::create(const std::string& lb_policy_name, const std::string& lb_config,
                              const std::string& metrics_namespace,
                              Envoy::Extensions::DynamicModules::DynamicModulePtr module,
                              Stats::Scope& stats_scope) {
  std::shared_ptr<DynamicModuleLbConfig> config(new DynamicModuleLbConfig(
      lb_policy_name, lb_config, metrics_namespace, std::move(module), stats_scope));

  // Resolve all required function pointers from the dynamic module.
#define RESOLVE_SYMBOL(name, type, member)                                                         \
  {                                                                                                \
    auto symbol_or_error = config->dynamic_module_->getFunctionPointer<type>(name);                \
    if (!symbol_or_error.ok()) {                                                                   \
      return symbol_or_error.status();                                                             \
    }                                                                                              \
    config->member = symbol_or_error.value();                                                      \
  }

  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_config_new", OnLbConfigNewType, on_config_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_config_destroy", OnLbConfigDestroyType,
                 on_config_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_new", OnLbNewType, on_lb_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_choose_host", OnLbChooseHostType, on_choose_host_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_on_host_membership_update",
                 OnLbOnHostMembershipUpdateType, on_host_membership_update_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_destroy", OnLbDestroyType, on_lb_destroy_);

#undef RESOLVE_SYMBOL

  // Call on_config_new to get the in-module configuration. The module can call
  // metric-defining callbacks during this invocation.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {config->lb_policy_name_.data(),
                                                        config->lb_policy_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {config->lb_config_.data(),
                                                          config->lb_config_.size()};

  config->in_module_config_ = config->on_config_new_(config.get(), name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("failed to create in-module load balancer configuration");
  }

  config->stat_creation_frozen_ = true;

  return config;
}

DynamicModuleLbConfig::DynamicModuleLbConfig(
    const std::string& lb_policy_name, const std::string& lb_config,
    const std::string& metrics_namespace,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope)
    : stats_scope_(stats_scope.createScope(absl::StrCat(metrics_namespace, "."))),
      stat_name_pool_(stats_scope_->symbolTable()), lb_policy_name_(lb_policy_name),
      lb_config_(lb_config), dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleLbConfig::~DynamicModuleLbConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
