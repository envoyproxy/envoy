#include "source/extensions/filters/http/dynamic_forward_proxy/config.h"

#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

absl::StatusOr<Http::FilterFactoryCb>
DynamicForwardProxyFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);
  Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory cluster_store_factory(
      context.serverFactoryContext().singletonManager());
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr cache_manager =
      cache_manager_factory.get();
  auto cache_or_error = cache_manager->getCache(proto_config.dns_cache_config());
  if (!cache_or_error.status().ok()) {
    return cache_or_error.status();
  }
  ProxyFilterConfigSharedPtr filter_config(std::make_shared<ProxyFilterConfig>(
      proto_config, std::move(cache_or_error.value()), std::move(cache_manager),
      cluster_store_factory, context));

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
