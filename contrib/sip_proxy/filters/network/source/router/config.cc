#include "contrib/sip_proxy/filters/network/source/router/config.h"

#include "envoy/registry/registry.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha/router.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha/router.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

SipFilters::FilterFactoryCb RouterFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::sip_proxy::router::v3alpha::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(proto_config);

  std::shared_ptr<RouterFilterConfig> config(
      new RouterFilterConfigImpl(proto_config, stat_prefix, context));

  return [config, &context,
          stat_prefix](SipFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(
        std::make_shared<Router>(config, context.serverFactoryContext().clusterManager(), context));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RouterFilterConfigFactory, SipFilters::NamedSipFilterConfigFactory);

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
