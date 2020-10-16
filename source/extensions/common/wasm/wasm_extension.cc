#include "extensions/common/wasm/wasm_extension.h"

#include "extensions/common/wasm/context.h"
#include "extensions/common/wasm/wasm.h"
#include "extensions/common/wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

WasmExtension* wasm_extension = nullptr;

} // namespace

Stats::ScopeSharedPtr WasmExtension::lockAndCreateStats(const Stats::ScopeSharedPtr& scope,
                                                        const PluginSharedPtr& plugin) {
  absl::MutexLock l(&mutex_);
  Stats::ScopeSharedPtr lock;
  if (!(lock = scope_.lock())) {
    resetStats();
    createStats(scope, plugin);
    scope_ = ScopeWeakPtr(scope);
    return scope;
  }
  createStats(scope, plugin);
  return lock;
}

void WasmExtension::resetStatsForTesting() {
  absl::MutexLock l(&mutex_);
  resetStats();
}

// Register a Wasm extension. Note: only one extension may be registered.
RegisterWasmExtension::RegisterWasmExtension(WasmExtension* extension) {
  RELEASE_ASSERT(!wasm_extension, "Multiple Wasm extensions registered.");
  wasm_extension = extension;
}

std::unique_ptr<EnvoyWasmVmIntegration>
EnvoyWasm::createEnvoyWasmVmIntegration(const Stats::ScopeSharedPtr& scope,
                                        absl::string_view runtime,
                                        absl::string_view short_runtime) {
  return std::make_unique<EnvoyWasmVmIntegration>(scope, runtime, short_runtime);
}

WasmHandleExtensionFactory EnvoyWasm::wasmFactory() {
  return [](const VmConfig vm_config, const Stats::ScopeSharedPtr& scope,
            Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher,
            Server::ServerLifecycleNotifier& lifecycle_notifier,
            absl::string_view vm_key) -> WasmHandleBaseSharedPtr {
    auto wasm = std::make_shared<Wasm>(vm_config.runtime(), vm_config.vm_id(),
                                       anyToBytes(vm_config.configuration()), vm_key, scope,
                                       cluster_manager, dispatcher);
    wasm->initializeLifecycle(lifecycle_notifier);
    return std::static_pointer_cast<WasmHandleBase>(std::make_shared<WasmHandle>(std::move(wasm)));
  };
}

WasmHandleExtensionCloneFactory EnvoyWasm::wasmCloneFactory() {
  return [](const WasmHandleSharedPtr& base_wasm, Event::Dispatcher& dispatcher,
            CreateContextFn create_root_context_for_testing) -> WasmHandleBaseSharedPtr {
    auto wasm = std::make_shared<Wasm>(base_wasm, dispatcher);
    wasm->setCreateContextForTesting(nullptr, create_root_context_for_testing);
    return std::static_pointer_cast<WasmHandleBase>(std::make_shared<WasmHandle>(std::move(wasm)));
  };
}

void EnvoyWasm::onEvent(WasmEvent event, const PluginSharedPtr&) {
  switch (event) {
  case WasmEvent::RemoteLoadCacheHit:
    create_wasm_stats_->remote_load_cache_hits_.inc();
    break;
  case WasmEvent::RemoteLoadCacheNegativeHit:
    create_wasm_stats_->remote_load_cache_negative_hits_.inc();
    break;
  case WasmEvent::RemoteLoadCacheMiss:
    create_wasm_stats_->remote_load_cache_misses_.inc();
    break;
  case WasmEvent::RemoteLoadCacheFetchSuccess:
    create_wasm_stats_->remote_load_fetch_successes_.inc();
    break;
  case WasmEvent::RemoteLoadCacheFetchFailure:
    create_wasm_stats_->remote_load_fetch_failures_.inc();
    break;
  default:
    break;
  }
}

void EnvoyWasm::onRemoteCacheEntriesChanged(int entries) {
  create_wasm_stats_->remote_load_cache_entries_.set(entries);
}

void EnvoyWasm::createStats(const Stats::ScopeSharedPtr& scope, const PluginSharedPtr&) {
  if (!create_wasm_stats_) {
    create_wasm_stats_.reset(new CreateWasmStats{CREATE_WASM_STATS( // NOLINT
        POOL_COUNTER_PREFIX(*scope, "wasm."), POOL_GAUGE_PREFIX(*scope, "wasm."))});
  }
}

void EnvoyWasm::resetStats() { create_wasm_stats_.reset(); }

WasmExtension* getWasmExtension() {
  static WasmExtension* extension = wasm_extension ? wasm_extension : new EnvoyWasm();
  return extension;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
