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

// Wasm VM base instance. Provides common behavior (e.g. Stats).
class WasmVmBase : public WasmVm {
public:
  WasmVmBase(const Stats::ScopeSharedPtr& scope, absl::string_view runtime)
      : scope_(scope), runtime_prefix_(absl::StrCat("wasm_vm.", runtime, ".")),
        runtime_(std::string(runtime)),
        stats_(VmStats{ALL_VM_STATS(POOL_COUNTER_PREFIX(*scope_, runtime_prefix_),
                                    POOL_GAUGE_PREFIX(*scope_, runtime_prefix_))}) {
    stats_.created_.inc();
    stats_.active_.inc();
    ENVOY_LOG(debug, "WasmVm created {} now active", runtime_, stats_.active_.value());
  }
  virtual ~WasmVmBase() {
    stats_.active_.dec();
    ENVOY_LOG(debug, "~WasmVm {} {} remaining active", runtime_, stats_.active_.value());
  }

protected:
  const Stats::ScopeSharedPtr scope_;
  const std::string runtime_prefix_;
  const std::string runtime_; // The runtime e.g. "v8".
  VmStats stats_;
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
