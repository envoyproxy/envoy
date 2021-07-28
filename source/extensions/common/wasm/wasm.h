#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>

#include "envoy/common/exception.h"
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
#include "source/common/stats/symbol_table_impl.h"
#include "source/common/version/version.h"
#include "source/extensions/common/wasm/context.h"
#include "source/extensions/common/wasm/plugin.h"
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

class WasmHandle;

// Wasm execution instance. Manages the Envoy side of the Wasm interface.
class Wasm : public WasmBase, Logger::Loggable<Logger::Id::wasm> {
public:
  Wasm(WasmConfig& config, absl::string_view vm_key, const Stats::ScopeSharedPtr& scope,
       Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher);
  Wasm(std::shared_ptr<WasmHandle> other, Event::Dispatcher& dispatcher);
  ~Wasm() override;

  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  Event::Dispatcher& dispatcher() { return dispatcher_; }
  Context* getRootContext(const std::shared_ptr<PluginBase>& plugin, bool allow_closed) {
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
  void log(const PluginSharedPtr& plugin, const Http::RequestHeaderMap* request_headers,
           const Http::ResponseHeaderMap* response_headers,
           const Http::ResponseTrailerMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info);

  void onStatsUpdate(const PluginSharedPtr& plugin, Envoy::Stats::MetricSnapshot& snapshot);

  virtual std::string buildVersion() { return BUILD_VERSION_NUMBER; }

  void initializeLifecycle(Server::ServerLifecycleNotifier& lifecycle_notifier);
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

  LifecycleStatsHandler& lifecycleStatsHandler() { return lifecycle_stats_handler_; }

protected:
  friend class Context;

  void initializeStats();
  // Calls into the VM.
  proxy_wasm::WasmCallVoid<3> on_resolve_dns_;
  proxy_wasm::WasmCallVoid<2> on_stats_update_;

  Stats::ScopeSharedPtr scope_;
  Upstream::ClusterManager& cluster_manager_;
  Event::Dispatcher& dispatcher_;
  Event::PostCb server_shutdown_post_cb_;
  absl::flat_hash_map<uint32_t, Event::TimerPtr> timer_; // per root_id.
  TimeSource& time_source_;

  // Lifecycle stats
  LifecycleStatsHandler lifecycle_stats_handler_;

  // Plugin stats
  absl::flat_hash_map<uint32_t, Stats::Counter*> counters_;
  absl::flat_hash_map<uint32_t, Stats::Gauge*> gauges_;
  absl::flat_hash_map<uint32_t, Stats::Histogram*> histograms_;

  CreateContextFn create_context_for_testing_;
  CreateContextFn create_root_context_for_testing_;
  Network::DnsResolverSharedPtr dns_resolver_;
  uint32_t dns_token_ = 1;
};
using WasmSharedPtr = std::shared_ptr<Wasm>;

class WasmHandle : public WasmHandleBase {
public:
  // Base Wasm.
  explicit WasmHandle(WasmConfig& wasm_config, const WasmSharedPtr& wasm)
      : WasmHandleBase(std::static_pointer_cast<WasmBase>(wasm)), wasm_(wasm), is_base_wasm_(true),
        max_restart_per_minute_(
            wasm_config.config().vm_config().has_restart_config()
                ? wasm_config.config().vm_config().restart_config().max_restart_per_minute()
                : 0) {}

  // Thread-local Wasm.
  explicit WasmHandle(const WasmSharedPtr& wasm)
      : WasmHandleBase(std::static_pointer_cast<WasmBase>(wasm)), wasm_(wasm) {}

  // Returns true if restart is allowed now for the given max_restart_per_minute in RestartConfig
  // and the previous restart counts *from this base wasm*. We adopt the simple sliding window
  // counters algorithm in the implementation.
  bool restartAllowed();

  WasmSharedPtr& wasm() { return wasm_; }

private:
  WasmSharedPtr wasm_;
  bool is_base_wasm_{false};

  // For restarts. Used only when is_base_wasm_ is true.
  absl::Mutex restart_counter_mutext_;
  uint32_t max_restart_per_minute_ = 0;
  struct RestartCountPerMinuteWindow {
    MonotonicTime window_key_;
    uint32_t count_ = 0;
  };
  RestartCountPerMinuteWindow current_restart_window_;
  RestartCountPerMinuteWindow prev_restart_window_;
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

// PluginHandleManager is responsible for recreating a plugin instance in case of VM failures for
// the same plugin configuration and base_wasm. tryRestartPlugin can be  used to restart at
// callsites where plugin's lifecycles vary.
class PluginHandleManager : public ThreadLocal::ThreadLocalObject,
                            Logger::Loggable<Logger::Id::wasm> {
public:
  PluginHandleManager(const WasmHandleSharedPtr base_wasm_handle, const PluginSharedPtr& plugin,
                      Event::Dispatcher& dispatcher,
                      CreateContextFn create_root_context_for_testing = nullptr)
      : base_wasm_handle_(base_wasm_handle), plugin_(plugin), dispatcher_(dispatcher),
        create_root_context_for_testing_(create_root_context_for_testing) {
    initializePluginHandle();
  };

  // Try to restart plugin and re-create plugin_handle_.
  // Returns true if the restart succeeded, false otherwise.
  bool tryRestartPlugin();

  PluginHandleSharedPtr& handle() { return handle_; }

private:
  bool initializePluginHandle();
  PluginHandleSharedPtr handle_{nullptr};

  // For creating plugin handle instances.
  const WasmHandleSharedPtr base_wasm_handle_;
  const PluginSharedPtr plugin_;
  Event::Dispatcher& dispatcher_;
  CreateContextFn create_root_context_for_testing_;
};

using PluginHandleManagerSharedPtr = std::shared_ptr<PluginHandleManager>;

using CreateWasmCallback = std::function<void(WasmHandleSharedPtr)>;

// Returns false if createWasm failed synchronously. This is necessary because xDS *MUST* report
// all failures synchronously as it has no facility to report configuration update failures
// asynchronously. Callers should throw an exception if they are part of a synchronous xDS update
// because that is the mechanism for reporting configuration errors.
bool createWasm(const PluginSharedPtr& plugin, const Stats::ScopeSharedPtr& scope,
                Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                Event::Dispatcher& dispatcher, Api::Api& api,
                Envoy::Server::ServerLifecycleNotifier& lifecycle_notifier,
                Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                CreateWasmCallback&& callback,
                CreateContextFn create_root_context_for_testing = nullptr);

void clearCodeCacheForTesting();
void setTimeOffsetForCodeCacheForTesting(MonotonicTime::duration d);
WasmEvent toWasmEvent(const std::shared_ptr<WasmHandleBase>& wasm);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
