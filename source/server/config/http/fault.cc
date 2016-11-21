#include "envoy/server/instance.h"

#include "common/http/filter/fault_filter.h"
#include "common/router/config_impl.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the fault injection filter. @see HttpFilterConfigFactory.
 */
class FaultFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& json_config,
                                             const std::string& stats_prefix,
                                             Server::Instance& server) override {
    if (type != HttpFilterType::Decoder || name != "fault") {
      return nullptr;
    }

    Http::FaultFilterConfigPtr config(
        new Http::FaultFilterConfig(json_config, server.runtime(), stats_prefix, server.stats()));
    return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{new Http::FaultFilter(config)});
    };
  }
};

/**
 * Static registration for the fault filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<FaultFilterConfig> register_;

} // Configuration
} // Server
