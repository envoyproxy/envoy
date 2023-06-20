// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace WasmStatsCpp {
NullPluginRegistry* context_registry_;
} // namespace WasmStatsCpp

RegisterNullVmPluginFactory register_wasm_speed_test_plugin("WasmStatsCpp", []() {
  return std::make_unique<NullPlugin>(WasmStatsCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
