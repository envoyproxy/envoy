#include "server/config/http/ip_tagging.h"

#include <string>

#include "common/http/filter/ip_tagging_filter.h"
#include "common/json/config_schemas.h"

#include "server/config/network/http_connection_manager.h"

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
 * Static registration for the ip tagging filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<IpTaggingFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
