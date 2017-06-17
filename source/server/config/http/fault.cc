#include "server/config/http/fault.h"

#include <string>

#include "common/http/filter/fault_filter.h"
#include "common/json/config_schemas.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb FaultFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                           const std::string& stats_prefix,
                                                           FactoryContext& context) {
  Http::FaultFilterConfigSharedPtr config(
      new Http::FaultFilterConfig(json_config, context.runtime(), stats_prefix, context.scope()));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::FaultFilter(config)});
  };
}

/**
 * Static registration for the fault filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<FaultFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
