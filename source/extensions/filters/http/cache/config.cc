#include "source/extensions/filters/http/cache/config.h"

#include "source/extensions/filters/http/cache/cache_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {
std::shared_ptr<HttpCache>
getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
         Server::Configuration::CommonFactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  HttpCacheFactory* const http_cache_factory =
      Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
  if (http_cache_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }
  return http_cache_factory->getCache(config, context);
}
} // namespace

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<CacheFilterConfig>(getCache(config, context), config);
  return [filter_config, stats_prefix,
          &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheFilter>(*filter_config, stats_prefix,
                                                            context.scope(), context.timeSource()));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CacheFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<CacheFilterConfig>(getCache(config, context), config);
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
