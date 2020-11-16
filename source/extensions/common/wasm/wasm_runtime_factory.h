#pragma once

#include <string_view>

#include "extensions/common/wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WasmRuntimeFactory {
public:
  virtual ~WasmRuntimeFactory() = default;
  virtual WasmVmPtr createWasmVm() PURE;

  virtual absl::string_view name() PURE;
  virtual absl::string_view shortName() PURE;

  std::string category() { return "envoy.wasm.runtime"; }
};

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
