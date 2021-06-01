#include "envoy/registry/registry.h"

#include "extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/wamr.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WamrRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createWamrVm(); }

  absl::string_view name() override { return "envoy.wasm.runtime.wamr"; }
};

#if defined(ENVOY_WASM_WAMR)
REGISTER_FACTORY(WamrRuntimeFactory, WasmRuntimeFactory);
#endif

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
