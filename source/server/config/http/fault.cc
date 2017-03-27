#include "server/config/http/fault.h"

#include "common/http/filter/fault_filter.h"
#include "common/json/config_schemas.h"

namespace Server {
namespace Configuration {

HttpFilterFactoryCb FaultFilterConfig::tryCreateFilterFactory(HttpFilterType type,
                                                              const std::string& name,
                                                              const Json::Object& json_config,
                                                              const std::string& stats_prefix,
                                                              Server::Instance& server) {
  if (type != HttpFilterType::Decoder || name != "fault") {
    return nullptr;
  }

  Http::FaultFilterConfigSharedPtr config(
      new Http::FaultFilterConfig(json_config, server.runtime(), stats_prefix, server.stats()));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::FaultFilter(config)});
  };
}

/**
 * Static registration for the fault filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<FaultFilterConfig> register_;

} // Configuration
} // Server
