#include "server/config/http/cors.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/registry/registry.h"

#include "common/http/filter/cors_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb CorsFilterConfig::createFilterFactory(const Json::Object&, const std::string&,
                                                          FactoryContext&) {

  Http::CorsFilterConfigConstSharedPtr config(new Http::CorsFilterConfig{});
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Http::CorsFilter(config)});
  };
}

/**
 * Static registration for the cors filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<CorsFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
