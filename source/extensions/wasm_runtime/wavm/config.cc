#include "envoy/registry/registry.h"

#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/wavm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WavmRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createWavmVm(); }

  std::string name() const override { return "envoy.wasm.runtime.wavm"; }
};

#if defined(PROXY_WASM_HAS_RUNTIME_WAVM)
REGISTER_FACTORY(WavmRuntimeFactory, WasmRuntimeFactory);
#endif

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
