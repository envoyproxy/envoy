#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/common/logger.h"

#include "absl/strings/str_cat.h"
#include "include/proxy-wasm/wasm_vm.h"
#include "include/proxy-wasm/word.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// Wasm VM data providing stats.
class EnvoyWasmVmIntegration : public proxy_wasm::WasmVmIntegration,
                               Logger::Loggable<Logger::Id::wasm> {
public:
  EnvoyWasmVmIntegration(const Stats::ScopeSharedPtr& scope, absl::string_view runtime,
                         absl::string_view short_runtime)
      : scope_(scope), runtime_(std::string(runtime)), short_runtime_(std::string(short_runtime)) {}

  // proxy_wasm::WasmVmIntegration
  proxy_wasm::WasmVmIntegration* clone() override {
    return new EnvoyWasmVmIntegration(scope_, runtime_, short_runtime_);
  }
  bool getNullVmFunction(absl::string_view function_name, bool returns_word,
                         int number_of_arguments, proxy_wasm::NullPlugin* plugin,
                         void* ptr_to_function_return) override;
  void error(absl::string_view message) override;

  const std::string& runtime() const { return runtime_; }

protected:
  const Stats::ScopeSharedPtr scope_;
  const std::string runtime_;
  const std::string short_runtime_;
}; // namespace Wasm

inline EnvoyWasmVmIntegration& getEnvoyWasmIntegration(proxy_wasm::WasmVm& wasm_vm) {
  return *static_cast<EnvoyWasmVmIntegration*>(wasm_vm.integration().get());
}

// Exceptions for issues with the WebAssembly code.
class WasmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

using WasmVmPtr = std::unique_ptr<proxy_wasm::WasmVm>;

// Create a new low-level Wasm VM using runtime of the given type (e.g. "envoy.wasm.runtime.wavm").
WasmVmPtr createWasmVm(absl::string_view runtime, const Stats::ScopeSharedPtr& scope);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
