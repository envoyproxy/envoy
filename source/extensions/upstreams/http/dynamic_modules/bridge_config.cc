#include "source/extensions/upstreams/http/dynamic_modules/bridge_config.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

absl::StatusOr<DynamicModuleHttpTcpBridgeConfigSharedPtr> DynamicModuleHttpTcpBridgeConfig::create(
    const std::string& bridge_name, const std::string& bridge_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr module) {
  std::shared_ptr<DynamicModuleHttpTcpBridgeConfig> config(
      new DynamicModuleHttpTcpBridgeConfig(bridge_name, bridge_config, std::move(module)));

  // Resolve all required function pointers from the dynamic module.
#define RESOLVE_SYMBOL(name, type, member)                                                         \
  {                                                                                                \
    auto symbol_or_error = config->dynamic_module_->getFunctionPointer<type>(name);                \
    if (!symbol_or_error.ok()) {                                                                   \
      return symbol_or_error.status();                                                             \
    }                                                                                              \
    config->member = symbol_or_error.value();                                                      \
  }

  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new",
                 OnHttpTcpBridgeConfigNewType, on_config_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy",
                 OnHttpTcpBridgeConfigDestroyType, on_config_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_new", OnHttpTcpBridgeNewType,
                 on_bridge_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers",
                 OnHttpTcpBridgeEncodeHeadersType, on_encode_headers_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data",
                 OnHttpTcpBridgeEncodeDataType, on_encode_data_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data",
                 OnHttpTcpBridgeOnUpstreamDataType, on_upstream_data_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy",
                 OnHttpTcpBridgeDestroyType, on_destroy_);

#undef RESOLVE_SYMBOL

  // Now call on_config_new to get the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {config->bridge_name_.data(),
                                                        config->bridge_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {config->bridge_config_.data(),
                                                          config->bridge_config_.size()};

  config->in_module_config_ = config->on_config_new_(name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("failed to create in-module bridge configuration");
  }

  return config;
}

DynamicModuleHttpTcpBridgeConfig::DynamicModuleHttpTcpBridgeConfig(
    const std::string& bridge_name, const std::string& bridge_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : in_module_config_(nullptr), bridge_name_(bridge_name), bridge_config_(bridge_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleHttpTcpBridgeConfig::~DynamicModuleHttpTcpBridgeConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
