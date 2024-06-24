#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::dynamic_forward_proxy::v3::FilterConfig;

/**
 * Config registration for the dynamic_forward_proxy filter. @see
 * NamedNetworkFilterConfigFactory.
 */
class DynamicForwardProxyNetworkFilterConfigFactory : public FactoryBase<FilterConfig> {
public:
  DynamicForwardProxyNetworkFilterConfigFactory();

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
