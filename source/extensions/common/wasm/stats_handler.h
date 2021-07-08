#pragma once

#include <memory>

#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/stats/symbol_table_impl.h"

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

struct CreateWasmStats {
  CREATE_WASM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

#define RUNTIME_STATS(COUNTER, GAUGE)                                                              \
  COUNTER(created)                                                                                 \
  GAUGE(active, NeverImport)

struct RuntimeStats {
  RUNTIME_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

using ScopeWeakPtr = std::weak_ptr<Stats::Scope>;

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
  VMCreated,
  VMShutDown,
};

class CreateStatsHandler : Logger::Loggable<Logger::Id::wasm> {
public:
  CreateStatsHandler() = default;
  ~CreateStatsHandler() = default;

  void initialize();

  void onEvent(WasmEvent event);
  void onRemoteCacheEntriesChanged(int remote_cache_entries);
  void createStats(const Stats::ScopeSharedPtr& scope) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void resetStats() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_); // Delete stats pointers

  // NB: the Scope can become invalid if, for example, the owning FilterChain is deleted. When that
  // happens the stats must be recreated. This hook verifies the Scope of any existing stats and if
  // necessary recreates the stats with the newly provided scope.
  // This call takes out the mutex_ and calls createStats and possibly resetStats().
  Stats::ScopeSharedPtr lockAndCreateStats(const Stats::ScopeSharedPtr& scope);

  void resetStatsForTesting();

protected:
  absl::Mutex mutex_;
  ScopeWeakPtr scope_;
  std::unique_ptr<CreateWasmStats> create_wasm_stats_;
};

CreateStatsHandler& getCreateStatsHandler();

class RuntimeStatsHandler {
public:
  RuntimeStatsHandler(const Stats::ScopeSharedPtr& scope, std::string runtime)
      : runtime_stats_(RuntimeStats{
            RUNTIME_STATS(POOL_COUNTER_PREFIX(*scope, absl::StrCat("wasm.", runtime, ".")),
                          POOL_GAUGE_PREFIX(*scope, absl::StrCat("wasm.", runtime, ".")))}){};
  ~RuntimeStatsHandler() = default;

  void onEvent(WasmEvent event);
  static int64_t getActiveVMCount();

protected:
  RuntimeStats runtime_stats_;
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
