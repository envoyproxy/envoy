#include "test/common/http/filters/route_cache_reset/config.h"

#include "test/common/http/filters/route_cache_reset/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouteCacheReset {

absl::StatusOr<Http::FilterFactoryCb>
RouteCacheResetFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::route_cache_reset::RouteCacheReset&,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {

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
