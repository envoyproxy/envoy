#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3/header_routing.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3/header_routing.pb.validate.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HeaderRouting {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::header_routing::v3::HeaderRouting;
using FilterFactoryCb = Network::UdpSessionFilterFactoryCb;

/**
 * Config registration for the header_routing UDP session filter. @see
 * NamedUdpSessionFilterConfigFactory.
 */
class HeaderRoutingUdpFilterConfigFactory : public FactoryBase<FilterConfig> {
public:
  HeaderRoutingUdpFilterConfigFactory();

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace HeaderRouting
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
