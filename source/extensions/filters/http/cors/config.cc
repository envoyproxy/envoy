#include "extensions/filters/http/cors/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/cors/cors_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

Server::Configuration::HttpFilterFactoryCb
CorsFilterConfig::createFilter(const std::string&, Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CorsFilter>());
  };
}

/**
 * Static registration for the cors filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<CorsFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
