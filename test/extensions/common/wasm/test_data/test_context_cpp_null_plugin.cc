// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace CommonWasmTestContextCpp {
NullPluginRegistry* context_registry_;
} // namespace CommonWasmTestContextCpp

RegisterNullVmPluginFactory
    register_common_wasm_test_context_cpp_plugin("CommonWasmTestContextCpp", []() {
      return std::make_unique<NullPlugin>(CommonWasmTestContextCpp::context_registry_);
    });

} // namespace null_plugin
} // namespace proxy_wasm
