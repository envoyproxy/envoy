#pragma once

#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/statusor.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

using OnNetworkConfigDestroyType = decltype(&envoy_dynamic_module_on_network_filter_config_destroy);
using OnNetworkFilterNewType = decltype(&envoy_dynamic_module_on_network_filter_new);
using OnNetworkFilterNewConnectionType =
    decltype(&envoy_dynamic_module_on_network_filter_new_connection);
using OnNetworkFilterReadType = decltype(&envoy_dynamic_module_on_network_filter_read);
using OnNetworkFilterWriteType = decltype(&envoy_dynamic_module_on_network_filter_write);
using OnNetworkFilterEventType = decltype(&envoy_dynamic_module_on_network_filter_event);
using OnNetworkFilterDestroyType = decltype(&envoy_dynamic_module_on_network_filter_destroy);
using OnNetworkFilterHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_network_filter_http_callout_done);
using OnNetworkFilterScheduledType = decltype(&envoy_dynamic_module_on_network_filter_scheduled);
using OnNetworkFilterConfigScheduledType =
    decltype(&envoy_dynamic_module_on_network_filter_config_scheduled);
using OnNetworkFilterAboveWriteBufferHighWatermarkType =
    decltype(&envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark);
using OnNetworkFilterBelowWriteBufferLowWatermarkType =
    decltype(&envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark);

// The default custom stat namespace which prepends all user-defined metrics.
// Note that the prefix is removed from the final output of ``/stats`` endpoints.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

class DynamicModuleNetworkFilterConfig;
using DynamicModuleNetworkFilterConfigSharedPtr = std::shared_ptr<DynamicModuleNetworkFilterConfig>;

/**
 * A config to create network filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the network filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleNetworkFilterConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleNetworkFilterConfig
    : public std::enable_shared_from_this<DynamicModuleNetworkFilterConfig> {
public:
  /**
   * Constructor for the config. Symbol resolution is done in newDynamicModuleNetworkFilterConfig().
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param metrics_namespace the namespace prefix for metrics.
   * @param dynamic_module the dynamic module to use.
   * @param cluster_manager the cluster manager for async HTTP callouts.
   * @param stats_scope the stats scope for metrics.
   * @param main_thread_dispatcher the main thread dispatcher for scheduling events.
   */
  DynamicModuleNetworkFilterConfig(const absl::string_view filter_name,
                                   const absl::string_view filter_config,
                                   const absl::string_view metrics_namespace,
                                   DynamicModulePtr dynamic_module,
                                   Envoy::Upstream::ClusterManager& cluster_manager,
                                   Stats::Scope& stats_scope,
                                   Event::Dispatcher& main_thread_dispatcher);

  ~DynamicModuleNetworkFilterConfig();

  /**
   * This is called when an event is scheduled via DynamicModuleNetworkFilterConfigScheduler.
   */
  void onScheduled(uint64_t event_id);

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_network_filter_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the network filter. All of them are resolved
  // during newDynamicModuleNetworkFilterConfig() and made sure they are not nullptr after that.

  OnNetworkConfigDestroyType on_network_filter_config_destroy_ = nullptr;
  OnNetworkFilterNewType on_network_filter_new_ = nullptr;
  OnNetworkFilterNewConnectionType on_network_filter_new_connection_ = nullptr;
  OnNetworkFilterReadType on_network_filter_read_ = nullptr;
  OnNetworkFilterWriteType on_network_filter_write_ = nullptr;
  OnNetworkFilterEventType on_network_filter_event_ = nullptr;
  OnNetworkFilterDestroyType on_network_filter_destroy_ = nullptr;
  OnNetworkFilterHttpCalloutDoneType on_network_filter_http_callout_done_ = nullptr;
  // Optional: modules that don't need scheduling don't need to implement this.
  OnNetworkFilterScheduledType on_network_filter_scheduled_ = nullptr;
  OnNetworkFilterConfigScheduledType on_network_filter_config_scheduled_ = nullptr;
  // Optional: modules that don't need watermark notifications don't need to implement these.
  OnNetworkFilterAboveWriteBufferHighWatermarkType
      on_network_filter_above_write_buffer_high_watermark_ = nullptr;
  OnNetworkFilterBelowWriteBufferLowWatermarkType
      on_network_filter_below_write_buffer_low_watermark_ = nullptr;

  Envoy::Upstream::ClusterManager& cluster_manager_;

  // The main thread dispatcher for scheduling config-level events.
  Event::Dispatcher& main_thread_dispatcher_;

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
  friend absl::StatusOr<std::shared_ptr<DynamicModuleNetworkFilterConfig>>
  newDynamicModuleNetworkFilterConfig(const absl::string_view filter_name,
                                      const absl::string_view filter_config,
                                      const absl::string_view metrics_namespace,
                                      DynamicModulePtr dynamic_module,
                                      Envoy::Upstream::ClusterManager& cluster_manager,
                                      Stats::Scope& stats_scope,
                                      Event::Dispatcher& main_thread_dispatcher);

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

/**
 * This class is used to schedule a network filter config event hook from a different thread
 * than the one it was assigned to. This is created via
 * envoy_dynamic_module_callback_network_filter_config_scheduler_new and deleted via
 * envoy_dynamic_module_callback_network_filter_config_scheduler_delete.
 */
class DynamicModuleNetworkFilterConfigScheduler {
public:
  DynamicModuleNetworkFilterConfigScheduler(std::weak_ptr<DynamicModuleNetworkFilterConfig> config,
                                            Event::Dispatcher& dispatcher)
      : config_(std::move(config)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleNetworkFilterConfig> config_shared = config.lock()) {
        config_shared->onScheduled(event_id);
      }
    });
  }

private:
  std::weak_ptr<DynamicModuleNetworkFilterConfig> config_;
  Event::Dispatcher& dispatcher_;
};

/**
 * Creates a new DynamicModuleNetworkFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param metrics_namespace the namespace prefix for metrics emitted by this module.
 * @param dynamic_module the dynamic module to use.
 * @param cluster_manager the cluster manager for async HTTP callouts.
 * @param stats_scope the stats scope for metrics.
 * @param main_thread_dispatcher the main thread dispatcher for scheduling events.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleNetworkFilterConfigSharedPtr> newDynamicModuleNetworkFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Envoy::Upstream::ClusterManager& cluster_manager, Stats::Scope& stats_scope,
    Event::Dispatcher& main_thread_dispatcher);

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
