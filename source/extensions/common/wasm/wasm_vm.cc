#include "extensions/common/wasm/wasm_vm.h"

#include <memory>

#include "extensions/common/wasm/null/null.h"
#include "extensions/common/wasm/v8/v8.h"
#include "extensions/common/wasm/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

thread_local Envoy::Extensions::Common::Wasm::Context* current_context_ = nullptr;
thread_local uint32_t effective_context_id_ = 0;

WasmVmPtr createWasmVm(absl::string_view runtime) {
  if (runtime.empty()) {
    throw WasmVmException("Failed to create WASM VM with unspecified runtime.");
  } else if (runtime == WasmRuntimeNames::get().Null) {
    return Null::createVm();
  } else if (runtime == WasmRuntimeNames::get().V8) {
    return V8::createVm();
  } else {
    throw WasmVmException(fmt::format(
        "Failed to create WASM VM using {} runtime. Envoy was compiled without support for it.",
        runtime));
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
