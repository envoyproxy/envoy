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
  WasmVmBase(Stats::ScopeSharedPtr scope, std::atomic<int>* active_vms_ptr,
             absl::string_view runtime)
      : scope_(scope), active_vms_ptr_(active_vms_ptr),
        runtime_prefix_(absl::StrCat("wasm_vm.", runtime, ".")),
        stats_(VmStats{ALL_VM_STATS(POOL_COUNTER_PREFIX(*scope_, runtime_prefix_),
                                    POOL_GAUGE_PREFIX(*scope_, runtime_prefix_))}),
        runtime_(std::string(runtime)) {
    stats_.created_.inc();
    (*active_vms_ptr_)++;
    stats_.active_.set(*active_vms_ptr_);
    ENVOY_LOG(debug, "WasmVm created {} now active", runtime_, *active_vms_ptr_);
  }
  virtual ~WasmVmBase() {
    (*active_vms_ptr_)--;
    // The stats referrd to by stats_ are resolved by name in the stats system.
    stats_.active_.set(*active_vms_ptr_);
    ENVOY_LOG(debug, "~WasmVm {} {} remaining active", runtime_, *active_vms_ptr_);
  }

protected:
  Stats::ScopeSharedPtr scope_;
  std::atomic<int>* active_vms_ptr_;
  std::string runtime_prefix_;
  VmStats stats_;
  std::string runtime_; // The runtime e.g. "v8".
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
