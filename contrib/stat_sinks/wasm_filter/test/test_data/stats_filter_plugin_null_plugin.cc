// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace StatsFilterTestPlugin {
NullPluginRegistry* context_registry_;
} // namespace StatsFilterTestPlugin

RegisterNullVmPluginFactory register_stats_filter_test_plugin("StatsFilterTestPlugin", []() {
  return std::make_unique<NullPlugin>(StatsFilterTestPlugin::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
