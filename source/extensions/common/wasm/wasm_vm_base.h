#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/common/wasm/wasm_vm.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

/**
 * Wasm host stats.
 */
#define ALL_VM_STATS(COUNTER, GAUGE)                                                               \
  COUNTER(created)                                                                                 \
  COUNTER(cloned)                                                                                  \
  GAUGE(active, NeverImport)

struct VmStats {
  ALL_VM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

struct VmGlobalStats {
  std::atomic<int> active_vms_{0};
};

// Wasm VM base instance. Provides common behavior (e.g. Stats).
class WasmVmBase : public WasmVm {
public:
  WasmVmBase(Stats::ScopeSharedPtr scope, VmGlobalStats& global_stats, absl::string_view runtime)
      : scope_(scope), runtime_prefix_(absl::StrCat("wasm_vm.", runtime, ".")),
        global_stats_(global_stats),
        stats_(VmStats{ALL_VM_STATS(POOL_COUNTER_PREFIX(*scope_, runtime_prefix_),
                                    POOL_GAUGE_PREFIX(*scope_, runtime_prefix_))}),
        runtime_(std::string(runtime)) {
    stats_.created_.inc();
    auto active = ++global_stats_.active_vms_;
    stats_.active_.set(active);
    ENVOY_LOG(debug, "WasmVm created {} now active", runtime_, active);
  }
  virtual ~WasmVmBase() {
    auto active = --global_stats_.active_vms_;
    // The stats referrd to by stats_ are resolved by name in the stats system.
    stats_.active_.set(active);
    ENVOY_LOG(debug, "~WasmVm {} {} remaining active", runtime_, active);
  }

protected:
  Stats::ScopeSharedPtr scope_;
  std::string runtime_prefix_;
  VmGlobalStats& global_stats_;
  VmStats stats_;
  std::string runtime_; // The runtime e.g. "v8".
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
