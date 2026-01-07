#pragma once

#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

class DynamicModuleUdpListenerFilterConfig {
public:
  DynamicModuleUdpListenerFilterConfig(
      const envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter&
          config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleUdpListenerFilterConfig();

  const std::string filter_name_;
  const std::string filter_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  envoy_dynamic_module_type_udp_listener_filter_config_module_ptr in_module_config_{nullptr};

  decltype(envoy_dynamic_module_on_udp_listener_filter_config_new)* on_filter_config_new_{nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_config_destroy)* on_filter_config_destroy_{
      nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_new)* on_filter_new_{nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_on_data)* on_filter_on_data_{nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_destroy)* on_filter_destroy_{nullptr};
};

using DynamicModuleUdpListenerFilterConfigSharedPtr =
    std::shared_ptr<DynamicModuleUdpListenerFilterConfig>;

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
