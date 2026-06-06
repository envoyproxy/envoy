#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnStatSinkConfigNewType = decltype(&envoy_dynamic_module_on_stat_sink_config_new);
using OnStatSinkConfigDestroyType = decltype(&envoy_dynamic_module_on_stat_sink_config_destroy);
using OnStatSinkFlushType = decltype(&envoy_dynamic_module_on_stat_sink_flush);
using OnStatSinkOnHistogramCompleteType =
    decltype(&envoy_dynamic_module_on_stat_sink_on_histogram_complete);
using OnStatSinkConfigScheduledType = decltype(&envoy_dynamic_module_on_stat_sink_config_scheduled);

/**
 * Configuration for a dynamic module stats sink. Holds the resolved module symbols and
 * the in-module config pointer. Multiple sink instances share this config object.
 *
 * Symbol resolution and in-module config creation are done in newDynamicModuleStatsSinkConfig()
 * to allow graceful error handling.
 */
class DynamicModuleStatsSinkConfig
    : public std::enable_shared_from_this<DynamicModuleStatsSinkConfig> {
public:
  /**
   * @param sink_name the name identifying the sink implementation within the module.
   * @param sink_config the configuration bytes for the sink.
   * @param dynamic_module the loaded dynamic module.
   * @param server the server factory context, used for the main thread dispatcher and stats scope.
   */
  DynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                               Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                               Server::Configuration::ServerFactoryContext& server);
  ~DynamicModuleStatsSinkConfig();

  // The corresponding in-module configuration pointer.
  envoy_dynamic_module_type_stat_sink_config_module_ptr in_module_config_{nullptr};

  // Required function pointers resolved in newDynamicModuleStatsSinkConfig(). All are guaranteed
  // non-null once construction succeeds.
  OnStatSinkConfigDestroyType on_config_destroy_{nullptr};
  OnStatSinkFlushType on_flush_{nullptr};
  OnStatSinkOnHistogramCompleteType on_histogram_complete_{nullptr};

  // Optional function pointer. Set only when the module implements the scheduled hook.
  OnStatSinkConfigScheduledType on_config_scheduled_{nullptr};

  // The main thread dispatcher used by the scheduler to post events back to the main thread.
  Event::Dispatcher& main_thread_dispatcher_;

  // Wraps a Stats::Gauge resolved during configuration creation so the module can publish values to
  // it later from the main thread.
  class ModuleGaugeHandle {
  public:
    explicit ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  /**
   * Defines a gauge with the given name and returns its 1-based id via gauge_id_out. Valid only
   * before stat creation is frozen, which happens once the configuration is created.
   */
  envoy_dynamic_module_type_metrics_result defineGauge(absl::string_view name,
                                                       size_t* gauge_id_out);

  /**
   * Sets a previously defined gauge to the given value. Must be called on the main thread.
   */
  envoy_dynamic_module_type_metrics_result setGauge(size_t gauge_id, uint64_t value);

  /**
   * Invokes the module's scheduled hook if present. Called on the main thread by the scheduler.
   */
  void onScheduled(uint64_t event_id);

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleStatsSinkConfig>>
  newDynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                                  Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                  Server::Configuration::ServerFactoryContext& server);

  const std::string sink_name_;
  const std::string sink_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // The config owns its scope so the gauges created from it stay valid for the config's lifetime,
  // regardless of server teardown order, and so do the gauge references cached from it.
  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;
  // Gauges are defined only during on_stat_sink_config_new and read afterwards, so the storage
  // needs no lock. The flag is stored with release ordering once creation finishes and loaded with
  // acquire ordering in defineGauge() so a module that defines gauges after the freeze is rejected.
  // This mirrors the HTTP filter config contract.
  std::atomic<bool> stat_creation_frozen_{false};
  std::vector<ModuleGaugeHandle> gauges_;
};

using DynamicModuleStatsSinkConfigSharedPtr = std::shared_ptr<DynamicModuleStatsSinkConfig>;

/**
 * Scheduler that dispatches events to a stats sink configuration on the main thread. The module can
 * hold it on any thread and call commit() to run the scheduled hook on the main thread.
 */
class DynamicModuleStatsSinkConfigScheduler {
public:
  explicit DynamicModuleStatsSinkConfigScheduler(std::weak_ptr<DynamicModuleStatsSinkConfig> config)
      : config_(std::move(config)) {}

  void commit(uint64_t event_id) {
    // Lock the config so its dispatcher member stays valid across post().
    auto config_shared = config_.lock();
    if (!config_shared) {
      return;
    }
    config_shared->main_thread_dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleStatsSinkConfig> cs = config.lock()) {
        cs->onScheduled(event_id);
      }
    });
  }

private:
  std::weak_ptr<DynamicModuleStatsSinkConfig> config_;
};

/**
 * Creates a new DynamicModuleStatsSinkConfig.
 * @param sink_name the name identifying the sink implementation within the module.
 * @param sink_config the configuration bytes for the sink.
 * @param dynamic_module the loaded dynamic module.
 * @param server the server factory context, used for the main thread dispatcher and stats scope.
 * @return a shared pointer to the config or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleStatsSinkConfigSharedPtr>
newDynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                Server::Configuration::ServerFactoryContext& server);

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
