#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace ExtAuthz {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::ext_authz::v3::FilterConfig;
using FilterFactoryCb = Network::UdpSessionFilterFactoryCb;

/**
 * Config registration for the UDP session ext_authz filter. @see
 * NamedUdpSessionFilterConfigFactory.
 */
class ExtAuthzFilterConfigFactory : public FactoryBase<FilterConfig> {
public:
  ExtAuthzFilterConfigFactory();

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace ExtAuthz
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
