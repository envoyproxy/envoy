#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

#include "absl/strings/string_view.h"
#include "include/proxy-wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using WasmVmPtr = std::unique_ptr<proxy_wasm::WasmVm>;

class WasmRuntimeFactory : public Config::UntypedFactory {
public:
  ~WasmRuntimeFactory() override = default;
  virtual WasmVmPtr createWasmVm() PURE;

  std::string category() const override { return "envoy.wasm.runtime"; }
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
