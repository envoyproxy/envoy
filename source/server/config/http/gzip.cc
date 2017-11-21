#include "server/config/http/gzip.h"

#include "envoy/registry/registry.h"

#include "common/http/filter/gzip_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GzipFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                          const std::string&, FactoryContext&) {
  Http::GzipFilterConfigSharedPtr config(new Http::GzipFilterConfig(json_config));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Http::GzipFilter(config)});
  };
}

/**
 * Static registration for the gzip filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GzipFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
