#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

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
using OnListenerFilterHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_listener_filter_http_callout_done);
using OnListenerFilterScheduledType = decltype(&envoy_dynamic_module_on_listener_filter_scheduled);
using OnListenerFilterConfigScheduledType =
    decltype(&envoy_dynamic_module_on_listener_filter_config_scheduled);

// The default custom stat namespace which prepends all user-defined metrics.
// Note that the prefix is removed from the final output of ``/stats`` endpoints.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

class DynamicModuleListenerFilterConfig;
using DynamicModuleListenerFilterConfigSharedPtr =
    std::shared_ptr<DynamicModuleListenerFilterConfig>;

/**
 * A config to create listener filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the listener filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleListenerFilterConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleListenerFilterConfig
    : public std::enable_shared_from_this<DynamicModuleListenerFilterConfig> {
public:
  /**
   * Constructor for the config. Symbol resolution is done in
   * newDynamicModuleListenerFilterConfig().
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param metrics_namespace the namespace prefix for metrics.
   * @param dynamic_module the dynamic module to use.
   * @param cluster_manager the cluster manager for async HTTP callouts.
   * @param stats_scope the stats scope for metrics.
   * @param main_thread_dispatcher the main thread dispatcher for scheduling events.
   */
  DynamicModuleListenerFilterConfig(const absl::string_view filter_name,
                                    const absl::string_view filter_config,
                                    const absl::string_view metrics_namespace,
                                    DynamicModulePtr dynamic_module,
                                    Envoy::Upstream::ClusterManager& cluster_manager,
                                    Stats::Scope& stats_scope,
                                    Event::Dispatcher& main_thread_dispatcher);

  ~DynamicModuleListenerFilterConfig();

  /**
   * This is called when an event is scheduled via DynamicModuleListenerFilterConfigScheduler.
   */
  void onScheduled(uint64_t event_id);

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
  // Optional: modules that don't need HTTP callout don't need to implement this.
  OnListenerFilterHttpCalloutDoneType on_listener_filter_http_callout_done_ = nullptr;
  // Optional: modules that don't need config-level scheduling don't need to implement this.
  OnListenerFilterScheduledType on_listener_filter_scheduled_ = nullptr;
  OnListenerFilterConfigScheduledType on_listener_filter_config_scheduled_ = nullptr;

  Envoy::Upstream::ClusterManager& cluster_manager_;

  // The main thread dispatcher for scheduling config-level events.
  Event::Dispatcher& main_thread_dispatcher_;

  // ----------------------------- Metrics Support -----------------------------
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }
  const Extensions::DynamicModules::MetricRegistry& metrics() const { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  // Shared metrics registry composed from stats_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during the in-module config_new, and not later from
  // worker threads, so that we don't have to wrap the metrics registry pool in a lock.
  bool stat_creation_frozen_ = false;

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleListenerFilterConfig>>
  newDynamicModuleListenerFilterConfig(const absl::string_view filter_name,
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
};

/**
 * This class is used to schedule a listener filter config event hook from a different thread
 * than the one it was assigned to. This is created via
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_new and deleted via
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_delete.
 */
class DynamicModuleListenerFilterConfigScheduler {
public:
  explicit DynamicModuleListenerFilterConfigScheduler(
      std::weak_ptr<DynamicModuleListenerFilterConfig> config)
      : config_(std::move(config)) {}

  void commit(uint64_t event_id) {
    // Lock the config so its dispatcher member stays valid across `post`.
    auto config_shared = config_.lock();
    if (!config_shared) {
      return;
    }
    config_shared->main_thread_dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleListenerFilterConfig> cs = config.lock()) {
        cs->onScheduled(event_id);
      }
    });
  }

private:
  std::weak_ptr<DynamicModuleListenerFilterConfig> config_;
};

/**
 * Creates a new DynamicModuleListenerFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param metrics_namespace the namespace prefix for metrics emitted by this module.
 * @param dynamic_module the dynamic module to use.
 * @param cluster_manager the cluster manager for async HTTP callouts.
 * @param stats_scope the stats scope for metrics.
 * @param main_thread_dispatcher the main thread dispatcher for scheduling events.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleListenerFilterConfigSharedPtr> newDynamicModuleListenerFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Envoy::Upstream::ClusterManager& cluster_manager, Stats::Scope& stats_scope,
    Event::Dispatcher& main_thread_dispatcher);

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
