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
        stats_(VmStats{
            ALL_VM_STATS(POOL_COUNTER_PREFIX(*scope_, absl::StrCat("wasm_vm.", runtime, ".")),
                         POOL_GAUGE_PREFIX(*scope_, absl::StrCat("wasm_vm.", runtime, ".")))}),
        runtime_(std::string(runtime)) {
    stats_.created_.inc();
    (*active_vms_ptr_)++;
    stats_.active_.set(*active_vms_ptr_);
    ENVOY_LOG(debug, "WasmVm created {} now active", runtime_, *active_vms_ptr_);
  }
  virtual ~WasmVmBase() {
    (*active_vms_ptr_)--;
    stats_.active_.set(*active_vms_ptr_);
    ENVOY_LOG(debug, "~WasmVm {} {} remaining active", runtime_, *active_vms_ptr_);
  }

protected:
  Stats::ScopeSharedPtr scope_;
  std::atomic<int>* active_vms_ptr_;
  VmStats stats_;
  std::string runtime_;
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
