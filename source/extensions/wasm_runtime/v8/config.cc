#include "envoy/registry/registry.h"

#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/v8.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class V8RuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createV8Vm(); }

  std::string name() const override { return "envoy.wasm.runtime.v8"; }
};

#if defined(PROXY_WASM_HAS_RUNTIME_V8)
REGISTER_FACTORY(V8RuntimeFactory, WasmRuntimeFactory);
#endif

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
