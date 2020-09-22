#include "extensions/filters/http/cache/config.h"

#include "extensions/filters/http/cache/cache_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  HttpCacheFactory* const http_cache_factory =
      Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
  if (http_cache_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  return [config, stats_prefix, &context,
          http_cache_factory](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheFilter>(config, stats_prefix, context.scope(),
                                                            context.timeSource(),
                                                            http_cache_factory->getCache(config)));
  };
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
