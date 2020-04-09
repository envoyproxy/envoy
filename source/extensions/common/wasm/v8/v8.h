#pragma once

#include <memory>

#include "extensions/common/wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace V8 {

WasmVmPtr createVm(const Stats::ScopeSharedPtr& scope);

} // namespace V8
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
