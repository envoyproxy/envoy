#include "extensions/filters/http/dynamic_forward_proxy/config.h"
#include "extensions/filters/http/dynamic_forward_proxy/dynamic_forward_proxy_proto_descriptors.h"
#include "extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "common/common/assert.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

Http::FilterFactoryCb DynamicForwardProxyFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::dynamic_forward_proxy::v2alpha::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  RELEASE_ASSERT(validateProtoDescriptors(), "");
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context.singletonManager(), context.dispatcher(), context.threadLocal(), context.scope());
  ProxyFilterConfigSharedPtr filter_config(std::make_shared<ProxyFilterConfig>(
      proto_config, cache_manager_factory, context.clusterManager()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<ProxyFilter>(filter_config));
  };
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
