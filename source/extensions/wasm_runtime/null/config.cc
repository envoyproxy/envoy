#include "envoy/registry/registry.h"

#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/null.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class NullRuntimeFactory : public WasmRuntimeFactory {
public:
  WasmVmPtr createWasmVm() override { return proxy_wasm::createNullVm(); }

  std::string name() const override { return "envoy.wasm.runtime.null"; }
};

REGISTER_FACTORY(NullRuntimeFactory, WasmRuntimeFactory);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
