#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/logger.h"

#include "absl/strings/str_cat.h"
#include "include/proxy-wasm/wasm_vm.h"
#include "include/proxy-wasm/word.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// providing logger and NullVm function getter to Wasm VM.
class EnvoyWasmVmIntegration : public proxy_wasm::WasmVmIntegration,
                               Logger::Loggable<Logger::Id::wasm> {
public:
  // proxy_wasm::WasmVmIntegration
  proxy_wasm::WasmVmIntegration* clone() override { return new EnvoyWasmVmIntegration(); }
  bool getNullVmFunction(std::string_view function_name, bool returns_word, int number_of_arguments,
                         proxy_wasm::NullPlugin* plugin, void* ptr_to_function_return) override;
  proxy_wasm::LogLevel getLogLevel() override;
  void error(std::string_view message) override;
  void trace(std::string_view message) override;
};

// Exceptions for issues with the WebAssembly code.
class WasmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

using WasmVmPtr = std::unique_ptr<proxy_wasm::WasmVm>;

// Create a new low-level Wasm VM using runtime of the given type (e.g.
// "envoy.wasm.runtime.wasmtime").
WasmVmPtr createWasmVm(absl::string_view runtime);

/**
 * @return true if the provided Wasm Engine is compiled with Envoy
 */
bool isWasmEngineAvailable(absl::string_view runtime);

/**
 * @return the name of the first available Wasm Engine compiled with Envoy
 */
absl::string_view getFirstAvailableWasmEngineName();

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
