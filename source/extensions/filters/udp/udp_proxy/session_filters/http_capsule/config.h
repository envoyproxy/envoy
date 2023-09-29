#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3/http_capsule.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3/http_capsule.pb.validate.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::http_capsule::v3::FilterConfig;

/**
 * Config registration for the http_capsule filter. @see
 * NamedNetworkFilterConfigFactory.
 */
class HttpCapsuleFilterConfigFactory : public FactoryBase<FilterConfig> {
public:
  HttpCapsuleFilterConfigFactory() : FactoryBase("envoy.filters.udp.session.http_capsule"){};

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
