#include "source/extensions/filters/http/cache/config.h"

#include "source/extensions/filters/http/cache/cache_filter.h"

#include "http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {
template <typename FactoryCtx>
HttpCachePtr getCache(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
                      FactoryCtx& ctx) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  auto* http_cache_factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
  if (Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type) == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  auto cache = http_cache_factory->getCache(config, ctx);
  if (cache == nullptr) {
    throw EnvoyException(fmt::format("Didn't find HTTP cache for type: '{}', config: '{}'", type,
                                     config.DebugString()));
  }

  return cache;
}
} // namespace

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  auto cache = getCache(config, context);

  return [config, stats_prefix, cache,
          &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheFilter>(stats_prefix, context.scope(),
                                                            context.timeSource(), config, cache));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
CacheFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {

  auto cache = getCache(config, context);
  return std::make_shared<CacheRouteFilterConfig>(cache, config);
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
