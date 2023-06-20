// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace AccessLoggerTestCpp {
NullPluginRegistry* context_registry_;
} // namespace AccessLoggerTestCpp

RegisterNullVmPluginFactory register_common_wasm_test_cpp_plugin("AccessLoggerTestCpp", []() {
  return std::make_unique<NullPlugin>(AccessLoggerTestCpp::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
