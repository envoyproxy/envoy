#pragma once

#include "envoy/common/optref.h"
#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/statusor.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

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

// Custom namespace prefix for access logger stats.
constexpr char AccessLogStatsNamespace[] = "dynamic_module_access_logger";

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
   * @param dynamic_module the dynamic module to use.
   * @param stats_scope the stats scope for metrics.
   */
  DynamicModuleAccessLogConfig(const absl::string_view logger_name,
                               const absl::string_view logger_config,
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
  // Handle classes for storing defined metrics.

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t value) const { counter_.add(value); }

  private:
    Stats::Counter& counter_;
  };

  class ModuleGaugeHandle {
  public:
    ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void add(uint64_t value) const { gauge_.add(value); }
    void sub(uint64_t value) const { gauge_.sub(value); }
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleHistogramHandle {
  public:
    ModuleHistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}
    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  // Methods for adding metrics during configuration.
  size_t addCounter(ModuleCounterHandle&& counter) {
    size_t id = counters_.size();
    counters_.push_back(std::move(counter));
    return id;
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    size_t id = gauges_.size();
    gauges_.push_back(std::move(gauge));
    return id;
  }

  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    size_t id = histograms_.size();
    histograms_.push_back(std::move(histogram));
    return id;
  }

  // Methods for getting metrics by ID.
  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id >= counters_.size()) {
      return {};
    }
    return counters_[id];
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id >= gauges_.size()) {
      return {};
    }
    return gauges_[id];
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id >= histograms_.size()) {
      return {};
    }
    return histograms_[id];
  }

  // Stats scope for metric creation.
  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleAccessLogConfig>>
  newDynamicModuleAccessLogConfig(const absl::string_view logger_name,
                                  const absl::string_view logger_config,
                                  Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                  Stats::Scope& stats_scope);

  // The name of the logger passed in the constructor.
  const std::string logger_name_;

  // The configuration bytes for the logger.
  const std::string logger_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // Metric storage.
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleHistogramHandle> histograms_;
};

using DynamicModuleAccessLogConfigSharedPtr = std::shared_ptr<DynamicModuleAccessLogConfig>;

/**
 * Creates a new DynamicModuleAccessLogConfig for the given configuration.
 * @param logger_name the name of the logger.
 * @param logger_config the configuration bytes for the logger.
 * @param dynamic_module the dynamic module to use.
 * @param stats_scope the stats scope for metrics.
 * @return a shared pointer to the new config object or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleAccessLogConfigSharedPtr> newDynamicModuleAccessLogConfig(
    const absl::string_view logger_name, const absl::string_view logger_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
