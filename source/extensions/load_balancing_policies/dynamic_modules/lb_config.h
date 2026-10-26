#pragma once

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

class DynamicModuleLbConfig;
using DynamicModuleLbConfigSharedPtr = std::shared_ptr<DynamicModuleLbConfig>;

/**
 * Function pointer types for the load balancer ABI functions.
 */
using OnLbConfigNewType = decltype(&envoy_dynamic_module_on_lb_config_new);
using OnLbConfigDestroyType = decltype(&envoy_dynamic_module_on_lb_config_destroy);
using OnLbNewType = decltype(&envoy_dynamic_module_on_lb_new);
using OnLbChooseHostType = decltype(&envoy_dynamic_module_on_lb_choose_host);
using OnLbOnHostMembershipUpdateType =
    decltype(&envoy_dynamic_module_on_lb_on_host_membership_update);
using OnLbDestroyType = decltype(&envoy_dynamic_module_on_lb_destroy);

/**
 * Configuration for a dynamic module load balancer. This holds the loaded dynamic module and
 * the resolved function pointers for the ABI.
 */
class DynamicModuleLbConfig : public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Creates a new DynamicModuleLbConfig.
   *
   * @param lb_policy_name the name identifying the load balancer implementation in the module.
   * @param lb_config the configuration bytes to pass to the module.
   * @param metrics_namespace the namespace prefix for metrics emitted by this module.
   * @param dynamic_module the loaded dynamic module.
   * @param stats_scope the stats scope for creating custom metrics.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<DynamicModuleLbConfigSharedPtr>
  create(const std::string& lb_policy_name, const std::string& lb_config,
         const std::string& metrics_namespace,
         Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
         Stats::Scope& stats_scope);

  ~DynamicModuleLbConfig();

  // Function pointers resolved from the dynamic module.
  OnLbConfigNewType on_config_new_;
  OnLbConfigDestroyType on_config_destroy_;
  OnLbNewType on_lb_new_;
  OnLbChooseHostType on_choose_host_;
  OnLbOnHostMembershipUpdateType on_host_membership_update_;
  OnLbDestroyType on_lb_destroy_;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_lb_config_module_ptr in_module_config_{nullptr};

  // ----------------------------- Metrics Support -----------------------------
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  // Shared metrics registry composed from stats_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during on_lb_config_new, and not later from worker
  // threads, so that we don't have to wrap the metrics registry pool in a lock.
  // Per-request label values use a stack-local Stats::StatNameDynamicPool in the increment
  // callbacks (see abi_impl.cc).
  bool stat_creation_frozen_ = false;

private:
  DynamicModuleLbConfig(const std::string& lb_policy_name, const std::string& lb_config,
                        const std::string& metrics_namespace,
                        Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                        Stats::Scope& stats_scope);

  const std::string lb_policy_name_;
  const std::string lb_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
