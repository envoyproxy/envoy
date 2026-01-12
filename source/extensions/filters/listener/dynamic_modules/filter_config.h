#pragma once

#include "envoy/common/optref.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/statusor.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

using OnListenerConfigDestroyType =
    decltype(&envoy_dynamic_module_on_listener_filter_config_destroy);
using OnListenerFilterNewType = decltype(&envoy_dynamic_module_on_listener_filter_new);
using OnListenerFilterOnAcceptType = decltype(&envoy_dynamic_module_on_listener_filter_on_accept);
using OnListenerFilterOnDataType = decltype(&envoy_dynamic_module_on_listener_filter_on_data);
using OnListenerFilterOnCloseType = decltype(&envoy_dynamic_module_on_listener_filter_on_close);
using OnListenerFilterGetMaxReadBytesType =
    decltype(&envoy_dynamic_module_on_listener_filter_get_max_read_bytes);
using OnListenerFilterDestroyType = decltype(&envoy_dynamic_module_on_listener_filter_destroy);

// Custom namespace prefix for listener filter stats.
constexpr char ListenerFilterStatsNamespace[] = "dynamic_module_listener_filter";

/**
 * A config to create listener filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the listener filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleListenerFilterConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleListenerFilterConfig {
public:
  /**
   * Constructor for the config. Symbol resolution is done in
   * newDynamicModuleListenerFilterConfig().
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param stats_scope the stats scope for metrics.
   */
  DynamicModuleListenerFilterConfig(const absl::string_view filter_name,
                                    const absl::string_view filter_config,
                                    DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

  ~DynamicModuleListenerFilterConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_listener_filter_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the listener filter. All of them are resolved
  // during newDynamicModuleListenerFilterConfig() and made sure they are not nullptr after that.

  OnListenerConfigDestroyType on_listener_filter_config_destroy_ = nullptr;
  OnListenerFilterNewType on_listener_filter_new_ = nullptr;
  OnListenerFilterOnAcceptType on_listener_filter_on_accept_ = nullptr;
  OnListenerFilterOnDataType on_listener_filter_on_data_ = nullptr;
  OnListenerFilterOnCloseType on_listener_filter_on_close_ = nullptr;
  OnListenerFilterGetMaxReadBytesType on_listener_filter_get_max_read_bytes_ = nullptr;
  OnListenerFilterDestroyType on_listener_filter_destroy_ = nullptr;

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
  friend absl::StatusOr<std::shared_ptr<DynamicModuleListenerFilterConfig>>
  newDynamicModuleListenerFilterConfig(const absl::string_view filter_name,
                                       const absl::string_view filter_config,
                                       DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // Metric storage.
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleHistogramHandle> histograms_;
};

using DynamicModuleListenerFilterConfigSharedPtr =
    std::shared_ptr<DynamicModuleListenerFilterConfig>;

/**
 * Creates a new DynamicModuleListenerFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @param stats_scope the stats scope for metrics.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleListenerFilterConfigSharedPtr> newDynamicModuleListenerFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
