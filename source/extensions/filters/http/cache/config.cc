#include "source/extensions/filters/http/cache/config.h"

#include "source/extensions/filters/http/cache/cache_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    const std::string& /*stats_prefix*/, Server::Configuration::FactoryContext& context) {
  std::shared_ptr<HttpCache> cache;
  if (!config.disabled().value()) {
    if (!config.has_typed_config()) {
      throw EnvoyException("at least one of typed_config or disabled must be set");
    }
    const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
    HttpCacheFactory* const http_cache_factory =
        Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
    if (http_cache_factory == nullptr) {
      throw EnvoyException(
          fmt::format("Didn't find a registered implementation for type: '{}'", type));
    }

    cache = http_cache_factory->getCache(config, context);
  }

  return [config = std::make_shared<CacheFilterConfig>(config, context.serverFactoryContext()),
          cache](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheFilter>(config, cache));
  };
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
