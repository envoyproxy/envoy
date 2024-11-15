// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace WasmStartCpp {
NullPluginRegistry* context_registry_;
} // namespace WasmStartCpp

RegisterNullVmPluginFactory register_wasm_speed_test_plugin("WasmStartCpp", []() {
  return std::make_unique<NullPlugin>(WasmStartCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
