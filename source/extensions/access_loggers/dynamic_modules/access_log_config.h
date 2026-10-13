#pragma once

#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnAccessLoggerConfigNewType = decltype(&envoy_dynamic_module_on_access_logger_config_new);
using OnAccessLoggerConfigDestroyType =
    decltype(&envoy_dynamic_module_on_access_logger_config_destroy);
using OnAccessLoggerNewType = decltype(&envoy_dynamic_module_on_access_logger_new);
using OnAccessLoggerLogType = decltype(&envoy_dynamic_module_on_access_logger_log);
using OnAccessLoggerDestroyType = decltype(&envoy_dynamic_module_on_access_logger_destroy);
using OnAccessLoggerFlushType = decltype(&envoy_dynamic_module_on_access_logger_flush);

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

/**
 * Configuration for dynamic module access loggers. This resolves and holds the symbols used for
 * access logging. Multiple access log instances may share this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleAccessLogConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleAccessLogConfig {
public:
  /**
   * Constructor for the config. Symbol resolution is done in newDynamicModuleAccessLogConfig().
   * @param logger_name the name of the logger.
   * @param logger_config the configuration bytes for the logger.
   * @param metrics_namespace the namespace prefix for metrics emitted by this module.
   * @param dynamic_module the dynamic module to use.
   * @param stats_scope the stats scope for metrics.
   */
  DynamicModuleAccessLogConfig(const absl::string_view logger_name,
                               const absl::string_view logger_config,
                               const absl::string_view metrics_namespace,
                               Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                               Stats::Scope& stats_scope);

  ~DynamicModuleAccessLogConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_access_logger_config_module_ptr in_module_config_{nullptr};

  // The function pointers for the module related to the access logger. All required ones are
  // resolved during newDynamicModuleAccessLogConfig() and guaranteed non-nullptr after that.
  OnAccessLoggerConfigDestroyType on_config_destroy_{nullptr};
  OnAccessLoggerNewType on_logger_new_{nullptr};
  OnAccessLoggerLogType on_logger_log_{nullptr};
  OnAccessLoggerDestroyType on_logger_destroy_{nullptr};
  // Optional flush callback. Called before logger destruction during shutdown.
  OnAccessLoggerFlushType on_logger_flush_{nullptr};

  // ----------------------------- Metrics Support -----------------------------
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  // Shared metrics registry composed from stats_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during the in-module config_new, and not later from
  // worker threads, so that we don't have to wrap the metrics registry pool in a lock.
  bool stat_creation_frozen_ = false;

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleAccessLogConfig>>
  newDynamicModuleAccessLogConfig(const absl::string_view logger_name,
                                  const absl::string_view logger_config,
                                  const absl::string_view metrics_namespace,
                                  Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                  Stats::Scope& stats_scope);

  // The name of the logger passed in the constructor.
  const std::string logger_name_;

  // The configuration bytes for the logger.
  const std::string logger_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleAccessLogConfigSharedPtr = std::shared_ptr<DynamicModuleAccessLogConfig>;

/**
 * Creates a new DynamicModuleAccessLogConfig for the given configuration.
 * @param logger_name the name of the logger.
 * @param logger_config the configuration bytes for the logger.
 * @param metrics_namespace the namespace prefix for metrics emitted by this module.
 * @param dynamic_module the dynamic module to use.
 * @param stats_scope the stats scope for metrics.
 * @return a shared pointer to the new config object or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleAccessLogConfigSharedPtr> newDynamicModuleAccessLogConfig(
    const absl::string_view logger_name, const absl::string_view logger_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
