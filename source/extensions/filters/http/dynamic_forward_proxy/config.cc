#include "extensions/filters/http/dynamic_forward_proxy/config.h"

#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.validate.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

Http::FilterFactoryCb DynamicForwardProxyFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context.singletonManager(), context.dispatcher(), context.threadLocal(), context.random(),
      context.scope());
  ProxyFilterConfigSharedPtr filter_config(std::make_shared<ProxyFilterConfig>(
      proto_config, cache_manager_factory, context.clusterManager()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<ProxyFilter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
DynamicForwardProxyFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig& config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const ProxyPerRouteConfig>(config);
}

/**
 * Static registration for the dynamic forward proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DynamicForwardProxyFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
