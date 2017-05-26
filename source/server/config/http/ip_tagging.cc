#include "server/config/http/ip_tagging.h"

#include <string>

#include "common/http/filter/ip_tagging_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb IpTaggingFilterConfig::tryCreateFilterFactory(HttpFilterType type,
                                                                  const std::string& name,
                                                                  const Json::Object& json_config,
                                                                  const std::string&,
                                                                  Server::Instance&) {
  if (type != HttpFilterType::Decoder || name != "ip_tagging") {
    return nullptr;
  }

  Http::IpTaggingFilterConfigSharedPtr config(
      new Http::IpTaggingFilterConfig(json_config));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::IpTaggingFilter(config)});
  };
}

/**
 * Static registration for the ip tagging filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<IpTaggingFilterConfig> register_;

} // Configuration
} // Server
} // Envoy