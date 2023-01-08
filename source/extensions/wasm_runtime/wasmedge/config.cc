#include "envoy/registry/registry.h"

#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/wasmedge.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WasmEdgeRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createWasmEdgeVm(); }

  std::string name() const override { return "envoy.wasm.runtime.wasmedge"; }
};

#if defined(PROXY_WASM_HAS_RUNTIME_WASMEDGE)
REGISTER_FACTORY(WasmEdgeRuntimeFactory, WasmRuntimeFactory);
#endif

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
