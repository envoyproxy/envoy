#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {

FilterFactoryCb HttpCapsuleFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig&, Server::Configuration::FactoryContext& context) {
  return [&context](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addFilter(
        std::make_shared<HttpCapsuleFilter>(context.serverFactoryContext().timeSource()));
  };
}

/**
 * Static registration for the http_capsule filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HttpCapsuleFilterConfigFactory, NamedUdpSessionFilterConfigFactory);

} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
