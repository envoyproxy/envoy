#include "server/config/http/ip_tagging.h"

#include <string>

#include "common/http/filter/ip_tagging_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb IpTaggingFilterConfig::createFilterFactory(HttpFilterType type,
                                                               const Json::Object& json_config,
                                                               const std::string&,
                                                               Server::Instance&) {
  if (type != HttpFilterType::Decoder) {
    throw EnvoyException(
        fmt::format("{} ip tagging filter must be configured as a decoder filter.", name()));
  }

  Http::IpTaggingFilterConfigSharedPtr config(new Http::IpTaggingFilterConfig(json_config));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::IpTaggingFilter(config)});
  };
}

std::string IpTaggingFilterConfig::name() { return "ip_tagging"; }

/**
 * Static registration for the ip tagging filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<IpTaggingFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
