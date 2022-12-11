#include "library/common/extensions/filters/http/route_cache_reset/config.h"

#include "library/common/extensions/filters/http/route_cache_reset/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouteCacheReset {

Http::FilterFactoryCb RouteCacheResetFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::route_cache_reset::RouteCacheReset&,
    const std::string&, Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<RouteCacheResetFilter>());
  };
}

/**
 * Static registration for the RouteCacheReset filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(RouteCacheResetFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RouteCacheReset
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
