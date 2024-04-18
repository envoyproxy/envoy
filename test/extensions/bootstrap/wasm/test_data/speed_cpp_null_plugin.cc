// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace WasmSpeedCpp {
NullPluginRegistry* context_registry_;
} // namespace WasmSpeedCpp

RegisterNullVmPluginFactory register_wasm_speed_test_plugin("WasmSpeedCpp", []() {
  return std::make_unique<NullPlugin>(WasmSpeedCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
