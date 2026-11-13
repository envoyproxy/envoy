// NOLINT(namespace-envoy)
#include "include/proxy-wasm/null_plugin.h"

namespace proxy_wasm {
namespace null_plugin {
namespace RedisCallNullPlugin {
NullPluginRegistry* context_registry_;
} // namespace RedisCallNullPlugin

RegisterNullVmPluginFactory register_redis_call_null_plugin("RedisCallNullPlugin", []() {
  return std::make_unique<NullPlugin>(RedisCallNullPlugin::context_registry_);
});

} // namespace null_plugin
} // namespace proxy_wasm
