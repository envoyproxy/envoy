#include "server/config/http/auth.h"

#include "envoy/registry/registry.h"

#include "common/http/filter/auth_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb AuthFilterConfig::createFilterFactory(const Json::Object&, const std::string&,
                                                          FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new Http::AuthFilter()});
  };
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<AuthFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
