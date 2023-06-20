#include "source/extensions/common/wasm/stats_handler.h"

#include "source/extensions/common/wasm/context.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/common/wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

Stats::ScopeSharedPtr CreateStatsHandler::lockAndCreateStats(const Stats::ScopeSharedPtr& scope) {
  absl::MutexLock l(&mutex_);
  Stats::ScopeSharedPtr lock;
  if (!(lock = scope_.lock())) {
    resetStats();
    createStats(scope);
    scope_ = ScopeWeakPtr(scope);
    return scope;
  }
  createStats(scope);
  return lock;
}

void CreateStatsHandler::resetStatsForTesting() {
  absl::MutexLock l(&mutex_);
  resetStats();
}

void CreateStatsHandler::onEvent(WasmEvent event) {
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

void CreateStatsHandler::onRemoteCacheEntriesChanged(int entries) {
  create_wasm_stats_->remote_load_cache_entries_.set(entries);
}

void CreateStatsHandler::createStats(const Stats::ScopeSharedPtr& scope) {
  if (!create_wasm_stats_) {
    create_wasm_stats_.reset(new CreateWasmStats{CREATE_WASM_STATS( // NOLINT
        POOL_COUNTER_PREFIX(*scope, "wasm."), POOL_GAUGE_PREFIX(*scope, "wasm."))});
  }
}

void CreateStatsHandler::resetStats() { create_wasm_stats_.reset(); }

CreateStatsHandler& getCreateStatsHandler() { MUTABLE_CONSTRUCT_ON_FIRST_USE(CreateStatsHandler); }

std::atomic<int64_t> active_wasms;

void LifecycleStatsHandler::onEvent(WasmEvent event) {
  switch (event) {
  case WasmEvent::VmShutDown:
    lifecycle_stats_.active_.set(--active_wasms);
    break;
  case WasmEvent::VmCreated:
    lifecycle_stats_.active_.set(++active_wasms);
    lifecycle_stats_.created_.inc();
    break;
  default:
    break;
  }
}

int64_t LifecycleStatsHandler::getActiveVmCount() { return active_wasms; };

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
