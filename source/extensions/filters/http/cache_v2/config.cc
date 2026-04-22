#include "source/extensions/filters/http/cache_v2/config.h"

#include "source/extensions/filters/http/cache_v2/cache_filter.h"
#include "source/extensions/filters/http/cache_v2/cache_sessions.h"
#include "source/extensions/filters/http/cache_v2/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
    const std::string& /*stats_prefix*/, Server::Configuration::FactoryContext& context) {
  std::shared_ptr<CacheSessions> cache;
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

    absl::StatusOr<std::shared_ptr<CacheSessions>> status_or_cache =
        http_cache_factory->getCache(config, context);
    if (!status_or_cache.ok()) {
      throw EnvoyException(fmt::format("Couldn't initialize cache: {}", status_or_cache.status()));
    }
    cache = *std::move(status_or_cache);
  }
  return
      [config = std::make_shared<CacheFilterConfig>(config, cache, context.serverFactoryContext())](
          Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<CacheFilter>(config));
      };
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
