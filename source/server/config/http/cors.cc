#include "server/config/http/cors.h"

#include "envoy/registry/registry.h"

#include "common/http/filter/cors_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb CorsFilterConfig::createFilter(const std::string&, FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Http::CorsFilter()});
  };
}

/**
 * Static registration for the cors filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<CorsFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
