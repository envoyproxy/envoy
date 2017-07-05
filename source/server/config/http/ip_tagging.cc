#include "server/config/http/ip_tagging.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/http/filter/ip_tagging_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb IpTaggingFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                               const std::string&,
                                                               FactoryContext&) {
  Http::IpTaggingFilterConfigSharedPtr config(new Http::IpTaggingFilterConfig(json_config));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::IpTaggingFilter(config)});
  };
}

/**
 * Static registration for the ip tagging filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpTaggingFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
