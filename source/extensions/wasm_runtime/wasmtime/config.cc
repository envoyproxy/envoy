#include "envoy/registry/registry.h"

#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/wasmtime.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WasmtimeRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createWasmtimeVm(); }

  std::string name() const override { return "envoy.wasm.runtime.wasmtime"; }
};

#if defined(PROXY_WASM_HAS_RUNTIME_WASMTIME)
REGISTER_FACTORY(WasmtimeRuntimeFactory, WasmRuntimeFactory);
#endif

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
