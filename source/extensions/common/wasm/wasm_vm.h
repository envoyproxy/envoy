#pragma once

#include <memory>

#include "absl/strings/str_cat.h"

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "include/proxy-wasm/wasm_vm.h"
#include "include/proxy-wasm/word.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using Word = proxy_wasm::Word;
using WasmVm = proxy_wasm::WasmVm;
using Cloneable = proxy_wasm::Cloneable;
using ContextBase = proxy_wasm::ContextBase;

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

// Wasm VM data providing stats.
class EnvoyWasmVmIntegration : public proxy_wasm::WasmVmIntegration,
                               Logger::Loggable<Logger::Id::wasm> {
public:
  EnvoyWasmVmIntegration(const Stats::ScopeSharedPtr& scope, absl::string_view runtime)
      : scope_(scope), runtime_prefix_(absl::StrCat("wasm_vm.", runtime, ".")),
        runtime_(std::string(runtime)),
        stats_(VmStats{ALL_VM_STATS(POOL_COUNTER_PREFIX(*scope_, runtime_prefix_),
                                    POOL_GAUGE_PREFIX(*scope_, runtime_prefix_))}) {
    stats_.created_.inc();
    stats_.active_.inc();
    ENVOY_LOG(debug, "WasmVm created {} now active", runtime_, stats_.active_.value());
  }
  virtual ~EnvoyWasmVmIntegration() {
    stats_.active_.dec();
    ENVOY_LOG(debug, "~WasmVm {} {} remaining active", runtime_, stats_.active_.value());
  }
  proxy_wasm::WasmVmIntegration* clone() override {
    return new EnvoyWasmVmIntegration(scope_, runtime_);
  }
  // void log(proxy_wasm::LogLevel level, absl::string_view message) override;
  void error(absl::string_view message) override;

protected:
  const Stats::ScopeSharedPtr scope_;
  const std::string runtime_prefix_;
  const std::string runtime_; // The runtime e.g. "v8".
  VmStats stats_;
}; // namespace Wasm

// Exceptions for issues with the WebAssembly code.
class WasmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

using WasmVmPtr = std::unique_ptr<WasmVm>;

// Create a new low-level WASM VM using runtime of the given type (e.g. "envoy.wasm.runtime.wavm").
WasmVmPtr createWasmVm(absl::string_view runtime, const Stats::ScopeSharedPtr& scope);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
