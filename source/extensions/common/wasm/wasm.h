#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/version/version.h"
#include "source/extensions/common/wasm/context.h"
#include "source/extensions/common/wasm/plugin.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"
#include "source/extensions/common/wasm/stats_handler.h"
#include "source/extensions/common/wasm/wasm_vm.h"

#include "include/proxy-wasm/exports.h"
#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using CreateContextFn =
    std::function<ContextBase*(Wasm* wasm, const std::shared_ptr<Plugin>& plugin)>;
using FailurePolicy = envoy::extensions::wasm::v3::FailurePolicy;

class WasmHandle;

// Wasm execution instance. Manages the Envoy side of the Wasm interface.
class Wasm : public WasmBase, Logger::Loggable<Logger::Id::wasm> {
public:
  Wasm(WasmConfig& config, absl::string_view vm_key, const Stats::ScopeSharedPtr& scope,
       Api::Api& api, Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher);
  Wasm(std::shared_ptr<WasmHandle> other, Event::Dispatcher& dispatcher);
  ~Wasm() override;

  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  Event::Dispatcher& dispatcher() { return dispatcher_; }
  Api::Api& api() { return api_; }
  Context* getRootContext(const std::shared_ptr<PluginBase>& plugin, bool allow_closed) override {
    return static_cast<Context*>(WasmBase::getRootContext(plugin, allow_closed));
  }
  void setTimerPeriod(uint32_t root_context_id, std::chrono::milliseconds period) override;
  virtual void tickHandler(uint32_t root_context_id);
  std::shared_ptr<Wasm> sharedThis() { return std::static_pointer_cast<Wasm>(shared_from_this()); }
  Network::DnsResolverSharedPtr& dnsResolver() { return dns_resolver_; }

  // WasmBase
  void error(std::string_view message) override;
  proxy_wasm::CallOnThreadFunction callOnThreadFunction() override;
  ContextBase* createContext(const std::shared_ptr<PluginBase>& plugin) override;
  ContextBase* createRootContext(const std::shared_ptr<PluginBase>& plugin) override;
  ContextBase* createVmContext() override;
  void registerCallbacks() override;
  void getFunctions() override;

  // AccessLog::Instance
  void log(const PluginSharedPtr& plugin, const Formatter::Context& log_context,
           const StreamInfo::StreamInfo& info);

  void onStatsUpdate(const PluginSharedPtr& plugin, Envoy::Stats::MetricSnapshot& snapshot);

  virtual std::string buildVersion() { return BUILD_VERSION_NUMBER; }

  uint32_t nextDnsToken() {
    do {
      dns_token_++;
    } while (!dns_token_);
    return dns_token_;
  }

  void setCreateContextForTesting(CreateContextFn create_context,
                                  CreateContextFn create_root_context) {
    create_context_for_testing_ = create_context;
    create_root_context_for_testing_ = create_root_context;
  }
  void setFailStateForTesting(proxy_wasm::FailState fail_state) { failed_ = fail_state; }

protected:
  friend class Context;

  void initializeStats();
  // Calls into the VM.
  proxy_wasm::WasmCallVoid<3> on_resolve_dns_;
  proxy_wasm::WasmCallVoid<2> on_stats_update_;

  Stats::ScopeSharedPtr scope_;
  Api::Api& api_;
  Stats::StatNamePool stat_name_pool_;
  const Stats::StatName custom_stat_namespace_;
  Upstream::ClusterManager& cluster_manager_;
  Event::Dispatcher& dispatcher_;
  absl::flat_hash_map<uint32_t, Event::TimerPtr> timer_; // per root_id.
  TimeSource& time_source_;

  // Lifecycle stats
  LifecycleStatsHandler lifecycle_stats_handler_;

  // A metric defined by the module through define_metric.
  template <class StatType> struct CustomMetric {
    // The name of the metric, including the custom stat namespace.
    Stats::StatName name;
    // The metric, if custom metrics are not evictable. May point to a null stat if the metric
    // could not be created.
    StatType* stat = nullptr;
    // Evictable mode only: the name of the metric in the central cache of the scope, set on the
    // first successful lookup. Used by get_metric, which must not re-create an evicted metric.
    Stats::StatName central_name;
    // Evictable gauges only: increments that were discarded because the gauge could not be
    // created, and that the next decrements on this thread must not apply.
    uint64_t discarded_increments = 0;
  };

  // Returns the metric, or nullptr if it currently cannot be created. In evictable mode the
  // metric is looked up on every call, and the returned pointer must not be retained.
  template <class StatType> StatType* customMetric(CustomMetric<StatType>& metric) {
    if (!custom_metrics_evictable_) {
      return metric.stat;
    }
    StatType& stat = lookupCustomMetric<StatType>(metric.name);
    // Null stats, returned for metrics that cannot be created, have an empty name.
    if (stat.statName().empty()) {
      return nullptr;
    }
    if (metric.central_name.empty()) {
      metric.central_name = stat_name_pool_.add(stat.statName());
    }
    return &stat;
  }

  template <class StatType>
  void defineCustomMetric(CustomMetric<StatType>& metric, Stats::StatName name) {
    metric.name = name;
    if (custom_metrics_evictable_) {
      // Create the metric when it is defined, as in non-evictable mode.
      customMetric(metric);
    } else {
      metric.stat = &lookupCustomMetric<StatType>(name);
    }
  }

  // Returns the value of the metric. In evictable mode, an evicted metric is not re-created and
  // its value is zero.
  template <class StatType> uint64_t customMetricValue(const CustomMetric<StatType>& metric) {
    if (!custom_metrics_evictable_) {
      return metric.stat->value();
    }
    if (metric.central_name.empty()) {
      return 0;
    }
    if constexpr (std::is_same_v<StatType, Stats::Counter>) {
      auto counter = custom_metrics_scope_->findCounter(metric.central_name);
      return counter.has_value() ? counter->value() : 0;
    } else {
      auto gauge = custom_metrics_scope_->findGauge(metric.central_name);
      return gauge.has_value() ? gauge->value() : 0;
    }
  }

  template <class StatType> StatType& lookupCustomMetric(Stats::StatName name) {
    if constexpr (std::is_same_v<StatType, Stats::Counter>) {
      return custom_metrics_scope_->counterFromStatName(name);
    } else if constexpr (std::is_same_v<StatType, Stats::Gauge>) {
      return custom_metrics_scope_->gaugeFromStatName(name, Stats::Gauge::ImportMode::Accumulate);
    } else {
      return custom_metrics_scope_->histogramFromStatName(name,
                                                          Stats::Histogram::Unit::Unspecified);
    }
  }

  // Custom metrics. Each clone of the VM runs on a single thread and has its own maps.
  Stats::ScopeSharedPtr custom_metrics_scope_;
  bool custom_metrics_evictable_ = false;
  // Metric ids by name, per metric type, so that defining the same metric again returns the
  // same id.
  std::array<absl::flat_hash_map<std::string, uint32_t>, 3> custom_metric_ids_;
  absl::flat_hash_map<uint32_t, CustomMetric<Stats::Counter>> counters_;
  absl::flat_hash_map<uint32_t, CustomMetric<Stats::Gauge>> gauges_;
  absl::flat_hash_map<uint32_t, CustomMetric<Stats::Histogram>> histograms_;

  CreateContextFn create_context_for_testing_;
  CreateContextFn create_root_context_for_testing_;
  Network::DnsResolverSharedPtr dns_resolver_;
  uint32_t dns_token_ = 1;
};
using WasmSharedPtr = std::shared_ptr<Wasm>;

class WasmHandle : public WasmHandleBase, public ThreadLocal::ThreadLocalObject {
public:
  explicit WasmHandle(const WasmSharedPtr& wasm)
      : WasmHandleBase(std::static_pointer_cast<WasmBase>(wasm)), wasm_(wasm) {}

  WasmSharedPtr& wasm() { return wasm_; }

private:
  WasmSharedPtr wasm_;
};

using WasmHandleSharedPtr = std::shared_ptr<WasmHandle>;

class PluginHandle : public PluginHandleBase {
public:
  explicit PluginHandle(const WasmHandleSharedPtr& wasm_handle, const PluginSharedPtr& plugin)
      : PluginHandleBase(std::static_pointer_cast<WasmHandleBase>(wasm_handle),
                         std::static_pointer_cast<PluginBase>(plugin)),
        plugin_(plugin), wasm_handle_(wasm_handle) {}

  WasmHandleSharedPtr& wasmHandle() { return wasm_handle_; }
  uint32_t rootContextId() { return wasm_handle_->wasm()->getRootContext(plugin_, false)->id(); }

private:
  PluginSharedPtr plugin_;
  WasmHandleSharedPtr wasm_handle_;
};

using PluginHandleSharedPtr = std::shared_ptr<PluginHandle>;

class PluginHandleSharedPtrThreadLocal : public ThreadLocal::ThreadLocalObject {
public:
  PluginHandleSharedPtr handle;
  MonotonicTime last_load;

  PluginHandleSharedPtrThreadLocal(PluginHandleSharedPtr h, MonotonicTime t = {})
      : handle(std::move(h)), last_load(t) {}
  PluginHandleSharedPtrThreadLocal() = default;
};

using CreateWasmCallback = std::function<void(WasmHandleSharedPtr)>;

// Returns false if createWasm failed synchronously. This is necessary because xDS *MUST* report
// all failures synchronously as it has no facility to report configuration update failures
// asynchronously. Callers should throw an exception if they are part of a synchronous xDS update
// because that is the mechanism for reporting configuration errors.
bool createWasm(const PluginSharedPtr& plugin, const Stats::ScopeSharedPtr& scope,
                Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                Event::Dispatcher& dispatcher, Api::Api& api,
                Server::ServerLifecycleNotifier& lifecycle_notifier,
                RemoteAsyncDataProviderPtr& remote_data_provider, CreateWasmCallback&& callback,
                CreateContextFn create_root_context_for_testing = nullptr);

PluginHandleSharedPtr
getOrCreateThreadLocalPlugin(const WasmHandleSharedPtr& base_wasm, const PluginSharedPtr& plugin,
                             Event::Dispatcher& dispatcher,
                             CreateContextFn create_root_context_for_testing = nullptr);

void clearCodeCacheForTesting();
void setTimeOffsetForCodeCacheForTesting(MonotonicTime::duration d);
WasmEvent toWasmEvent(const std::shared_ptr<WasmHandleBase>& wasm);

class PluginConfig : Logger::Loggable<Logger::Id::wasm> {
public:
  // TODO(wbpcode): the code of PluginConfig will be shared cross all Wasm extensions (loggers,
  // http filters, etc.), we may extend the constructor to takes a static string view to tell
  // the type of the plugin if needed.
  PluginConfig(const envoy::extensions::wasm::v3::PluginConfig& config,
               Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
               Init::Manager& init_manager, bool singleton);

  std::shared_ptr<Context> createContext();
  Wasm* wasm();
  const PluginSharedPtr& plugin() { return plugin_; }
  WasmStats& wasmStats() { return stats_handler_->wasmStats(); }

  using SinglePluginHandle = PluginHandleSharedPtrThreadLocal;
  using ThreadLocalPluginHandle = ThreadLocal::TypedSlotPtr<SinglePluginHandle>;

private:
  /**
   * Get the latest wasm and plugin handle wrapper. The plugin handle may be reloaded if
   * the wasm is failed and the policy allows it.
   */
  std::pair<OptRef<SinglePluginHandle>, Wasm*> getPluginHandleAndWasm();

  /**
   * May reload the handle if the wasm if failed. The input handle will be updated if the
   * handle is reloaded.
   * @return the wasm pointer of the latest handle.
   */
  Wasm* maybeReloadHandleIfNeeded(SinglePluginHandle& handle_wrapper);

  StatsHandlerSharedPtr stats_handler_;
  FailurePolicy failure_policy_;
  // This backoff strategy implementation is thread-safe and could be shared across multiple
  // workers.
  std::unique_ptr<JitteredLowerBoundBackOffStrategy> reload_backoff_;
  PluginSharedPtr plugin_;
  RemoteAsyncDataProviderPtr remote_data_provider_;
  const bool is_singleton_handle_{};
  WasmHandleSharedPtr base_wasm_;
  absl::variant<absl::monostate, SinglePluginHandle, ThreadLocalPluginHandle> plugin_handle_;
};

using PluginConfigPtr = std::unique_ptr<PluginConfig>;
using PluginConfigSharedPtr = std::shared_ptr<PluginConfig>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
