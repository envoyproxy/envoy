#include "extensions/filters/network/sip_proxy/router/config.h"

#include "envoy/extensions/filters/network/sip_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/sip_proxy/router/v3/router.pb.validate.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/network/sip_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

SipFilters::FilterFactoryCb RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::sip_proxy::router::v3::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(proto_config);

  return [&context, stat_prefix](SipFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(
        std::make_shared<Router>(context.clusterManager(), stat_prefix, context.scope()));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RouterFilterConfig, SipFilters::NamedSipFilterConfigFactory);

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
