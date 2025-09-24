#pragma once

#include <string>

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

/**
 * Constant values used for tracing metadata.
 */
struct TracingConstantValues {
  const std::string TracePluginName = "wasm_plugin_name";
  const std::string TraceVmId = "wasm_vm_id";
  const std::string TraceEngine = "wasm_engine";
  const std::string TraceRootId = "wasm_root_id";
  const std::string TraceEndOfStream = "end_of_stream";
};

using TracingConstants = ConstSingleton<TracingConstantValues>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
