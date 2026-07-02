#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::dynamic_modules::v3::DynamicModuleSessionFilter;
using FilterFactoryCb = Network::UdpSessionFilterFactoryCb;

class DynamicModuleUdpSessionFilterConfigFactory : public FactoryBase<FilterConfig> {
public:
  DynamicModuleUdpSessionFilterConfigFactory()
      : FactoryBase("envoy.filters.udp.session.dynamic_modules") {}

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
