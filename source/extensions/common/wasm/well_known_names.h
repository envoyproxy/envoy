#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

/**
 * Well-known wasm VM names.
 * NOTE: New wasm VMs should use the well known name: envoy.wasm.vm.name.
 */
class WasmVmValues {
public:
  // Null sandbox: modules must be compiled into envoy and registered name is given in the
  // DataSource.inline_string.
  const std::string Null = "envoy.wasm.vm.null";
};

using WasmVmNames = ConstSingleton<WasmVmValues>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
