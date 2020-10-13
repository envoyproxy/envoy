#pragma once

#include <memory>

#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/common/wasm/context.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

#define CREATE_WASM_STATS(COUNTER, GAUGE)                                                          \
  COUNTER(remote_load_cache_hits)                                                                  \
  COUNTER(remote_load_cache_negative_hits)                                                         \
  COUNTER(remote_load_cache_misses)                                                                \
  COUNTER(remote_load_fetch_successes)                                                             \
  COUNTER(remote_load_fetch_failures)                                                              \
  GAUGE(remote_load_cache_entries, NeverImport)

class WasmHandle;
class EnvoyWasmVmIntegration;

using WasmHandleSharedPtr = std::shared_ptr<WasmHandle>;
using CreateContextFn =
    std::function<ContextBase*(Wasm* wasm, const std::shared_ptr<Plugin>& plugin)>;
using WasmHandleExtensionFactory = std::function<WasmHandleBaseSharedPtr(
    const VmConfig& vm_config, const Stats::ScopeSharedPtr& scope,
    Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher,
    Server::ServerLifecycleNotifier& lifecycle_notifier, absl::string_view vm_key)>;
using WasmHandleExtensionCloneFactory = std::function<WasmHandleBaseSharedPtr(
    const WasmHandleSharedPtr& base_wasm, Event::Dispatcher& dispatcher,
    CreateContextFn create_root_context_for_testing)>;
using ScopeWeakPtr = std::weak_ptr<Stats::Scope>;

struct CreateWasmStats {
  CREATE_WASM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// Extension point for Wasm clients in embedded Envoy.
class WasmExtension : Logger::Loggable<Logger::Id::wasm> {
public:
  WasmExtension() = default;
  virtual ~WasmExtension() = default;

  virtual void initialize() = 0;
  virtual std::unique_ptr<EnvoyWasmVmIntegration>
  createEnvoyWasmVmIntegration(const Stats::ScopeSharedPtr& scope, absl::string_view runtime,
                               absl::string_view short_runtime) = 0;
  virtual WasmHandleExtensionFactory wasmFactory() = 0;
  virtual WasmHandleExtensionCloneFactory wasmCloneFactory() = 0;
  enum class WasmEvent : int {
    Ok,
    RemoteLoadCacheHit,
    RemoteLoadCacheNegativeHit,
    RemoteLoadCacheMiss,
    RemoteLoadCacheFetchSuccess,
    RemoteLoadCacheFetchFailure,
    UnableToCreateVM,
    UnableToCloneVM,
    MissingFunction,
    UnableToInitializeCode,
    StartFailed,
    ConfigureFailed,
    RuntimeError,
  };
  virtual void onEvent(WasmEvent event, const PluginSharedPtr& plugin) = 0;
  virtual void onRemoteCacheEntriesChanged(int remote_cache_entries) = 0;
  virtual void createStats(const Stats::ScopeSharedPtr& scope, const PluginSharedPtr& plugin)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;
  virtual void resetStats() EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0; // Delete stats pointers

  // NB: the Scope can become invalid if, for example, the owning FilterChain is deleted. When that
  // happens the stats must be recreated. This hook verifies the Scope of any existing stats and if
  // necessary recreates the stats with the newly provided scope.
  // This call takes out the mutex_ and calls createStats and possibly resetStats().
  Stats::ScopeSharedPtr lockAndCreateStats(const Stats::ScopeSharedPtr& scope,
                                           const PluginSharedPtr& plugin);

  void resetStatsForTesting();

protected:
  absl::Mutex mutex_;
  ScopeWeakPtr scope_;
};

// The default Envoy Wasm implementation.
class EnvoyWasm : public WasmExtension {
public:
  EnvoyWasm() = default;
  ~EnvoyWasm() override = default;
  void initialize() override {}
  std::unique_ptr<EnvoyWasmVmIntegration>
  createEnvoyWasmVmIntegration(const Stats::ScopeSharedPtr& scope, absl::string_view runtime,
                               absl::string_view short_runtime) override;
  WasmHandleExtensionFactory wasmFactory() override;
  WasmHandleExtensionCloneFactory wasmCloneFactory() override;
  void onEvent(WasmEvent event, const PluginSharedPtr& plugin) override;
  void onRemoteCacheEntriesChanged(int remote_cache_entries) override;
  void createStats(const Stats::ScopeSharedPtr& scope, const PluginSharedPtr& plugin) override;
  void resetStats() override;

private:
  std::unique_ptr<CreateWasmStats> create_wasm_stats_;
};

// Register a Wasm extension. Note: only one extension may be registered.
struct RegisterWasmExtension {
  RegisterWasmExtension(WasmExtension* extension);
};
#define REGISTER_WASM_EXTENSION(_class)                                                            \
  ::Envoy::Extensions::Common::Wasm::RegisterWasmExtension register_wasm_extension(new _class());

WasmExtension* getWasmExtension();

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
