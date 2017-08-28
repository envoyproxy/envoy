#include "server/config/http/gzip.h"

#include "envoy/registry/registry.h"

#include "common/http/filter/gzip_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GzipFilterConfig::createFilterFactory(const Json::Object&, const std::string&,
                                                          FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Http::GzipFilter()});
  };
}

static Registry::RegisterFactory<GzipFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
