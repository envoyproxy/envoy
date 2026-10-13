// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace CommonWasmTestCpp {
NullPluginRegistry* context_registry_;
} // namespace CommonWasmTestCpp

RegisterNullVmPluginFactory register_common_wasm_test_cpp_plugin("CommonWasmTestCpp", []() {
  return std::make_unique<NullPlugin>(CommonWasmTestCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
