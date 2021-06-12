// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace CommonWasmRestrictionTestCpp {
NullPluginRegistry* context_registry_;
} // namespace CommonWasmRestrictionTestCpp

RegisterNullVmPluginFactory
    register_common_wasm_test_restriction_cpp_plugin("CommonWasmTestRestrictionCpp", []() {
      return std::make_unique<NullPlugin>(CommonWasmRestrictionTestCpp::context_registry_);
    });

} // namespace null_plugin
} // namespace proxy_wasm
