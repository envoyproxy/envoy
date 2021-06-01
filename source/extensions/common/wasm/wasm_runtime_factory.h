#pragma once

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "include/proxy-wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using WasmVmPtr = std::unique_ptr<proxy_wasm::WasmVm>;

class WasmRuntimeFactory {
public:
  virtual ~WasmRuntimeFactory() = default;
  virtual WasmVmPtr createWasmVm() PURE;

  virtual absl::string_view name() PURE;

  std::string category() { return "envoy.wasm.runtime"; }
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
