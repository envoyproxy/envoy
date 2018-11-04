#include "extensions/filters/http/cors/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/cors/cors_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

Http::FilterFactoryCb
CorsFilterFactory::createFilter(const std::string& stats_prefix,
                                Server::Configuration::FactoryContext& context) {
  CorsFilterConfigSharedPtr config =
      std::make_shared<CorsFilterConfig>(stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CorsFilter>(config));
  };
}

/**
 * Static registration for the cors filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<CorsFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
