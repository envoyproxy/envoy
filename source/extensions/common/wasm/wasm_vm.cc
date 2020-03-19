#include "extensions/common/wasm/wasm_vm.h"

#include <memory>

#include "extensions/common/wasm/well_known_names.h"

#include "include/proxy-wasm/null.h"
#include "include/proxy-wasm/v8.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

void EnvoyWasmVmIntegration::error(absl::string_view message) {
  throw WasmException(std::string(message));
}

WasmVmPtr createWasmVm(absl::string_view runtime, const Stats::ScopeSharedPtr& scope) {
  if (runtime.empty()) {
    throw WasmException("Failed to create WASM VM with unspecified runtime.");
  } else if (runtime == WasmRuntimeNames::get().Null) {
    auto wasm = proxy_wasm::createNullVm();
    wasm->integration().reset(new EnvoyWasmVmIntegration(scope, runtime, "null"));
    return wasm;
  } else if (runtime == WasmRuntimeNames::get().V8) {
    auto wasm = proxy_wasm::createV8Vm();
    wasm->integration().reset(new EnvoyWasmVmIntegration(scope, runtime, "v8"));
    return wasm;
  } else {
    throw WasmException(fmt::format(
        "Failed to create WASM VM using {} runtime. Envoy was compiled without support for it.",
        runtime));
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
