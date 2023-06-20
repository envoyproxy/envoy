// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace NetworkTestCpp {
NullPluginRegistry* context_registry_;
} // namespace NetworkTestCpp

RegisterNullVmPluginFactory register_common_wasm_test_cpp_plugin("NetworkTestCpp", []() {
  return std::make_unique<NullPlugin>(NetworkTestCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
