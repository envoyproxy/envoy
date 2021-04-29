// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace HttpWasmTestCpp {
NullPluginRegistry* context_registry_;
} // namespace HttpWasmTestCpp

RegisterNullVmPluginFactory register_common_wasm_test_cpp_plugin("HttpWasmTestCpp", []() {
  return std::make_unique<NullPlugin>(HttpWasmTestCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
