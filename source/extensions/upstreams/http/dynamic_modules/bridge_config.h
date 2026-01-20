#pragma once

#include <memory>
#include <string>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

class DynamicModuleHttpTcpBridgeConfig;
using DynamicModuleHttpTcpBridgeConfigSharedPtr = std::shared_ptr<DynamicModuleHttpTcpBridgeConfig>;

/**
 * Function pointer types for the upstream HTTP-TCP bridge ABI functions.
 */
using OnHttpTcpBridgeConfigNewType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new);
using OnHttpTcpBridgeConfigDestroyType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy);
using OnHttpTcpBridgeNewType = decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_new);
using OnHttpTcpBridgeEncodeHeadersType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers);
using OnHttpTcpBridgeEncodeDataType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data);
using OnHttpTcpBridgeOnUpstreamDataType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data);
using OnHttpTcpBridgeDestroyType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy);

/**
 * Configuration for a dynamic module HTTP-TCP bridge. This holds the loaded dynamic module and
 * the resolved function pointers for the ABI.
 */
class DynamicModuleHttpTcpBridgeConfig {
public:
  /**
   * Creates a new DynamicModuleHttpTcpBridgeConfig.
   *
   * @param bridge_name the name identifying the bridge implementation in the module.
   * @param bridge_config the configuration bytes to pass to the module.
   * @param dynamic_module the loaded dynamic module.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<DynamicModuleHttpTcpBridgeConfigSharedPtr>
  create(const std::string& bridge_name, const std::string& bridge_config,
         Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleHttpTcpBridgeConfig();

  // Function pointers resolved from the dynamic module.
  OnHttpTcpBridgeConfigNewType on_config_new_;
  OnHttpTcpBridgeConfigDestroyType on_config_destroy_;
  OnHttpTcpBridgeNewType on_bridge_new_;
  OnHttpTcpBridgeEncodeHeadersType on_encode_headers_;
  OnHttpTcpBridgeEncodeDataType on_encode_data_;
  OnHttpTcpBridgeOnUpstreamDataType on_upstream_data_;
  OnHttpTcpBridgeDestroyType on_destroy_;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr in_module_config_;

private:
  DynamicModuleHttpTcpBridgeConfig(
      const std::string& bridge_name, const std::string& bridge_config,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string bridge_name_;
  const std::string bridge_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
