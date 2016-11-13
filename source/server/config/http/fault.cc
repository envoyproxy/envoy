#include "envoy/server/instance.h"

#include "common/http/filter/fault_filter.h"
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

    /**
     * TODO: Throw error if invalid return code is provided
     */
    uint32_t delay_duration = static_cast<uint32_t>(json_config.getInteger("delay_duration", 0));
    uint32_t delay_probability =
        static_cast<uint32_t>(json_config.getInteger("delay_probability", 0));
    uint32_t abort_code = static_cast<uint32_t>(json_config.getInteger("abort_code", 0));
    uint32_t abort_probability =
        static_cast<uint32_t>(json_config.getInteger("abort_probability", 0));
    if (delay_probability > 0) {
      if (delay_probability > 100) {
        throw EnvoyException("delay probability cannot be greater than 100");
      }
      if (0 == delay_duration) {
        throw EnvoyException("delay duration cannot be 0 when delay probability is greater than 1");
      }
    }

    if (abort_probability > 0) {
      if (abort_probability > 100) {
        throw EnvoyException("abort probability cannot be greater than 100");
      }
    }

    Http::FaultFilterConfigPtr config(new Http::FaultFilterConfig{
        Http::FaultFilter::generateStats(stats_prefix, server.stats()), delay_duration,
        delay_probability, abort_code, abort_probability});
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
