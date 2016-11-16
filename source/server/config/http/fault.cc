#include "envoy/http/header_map.h"
#include "envoy/server/instance.h"

#include "common/common/empty_string.h"
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

    // TODO: Throw error if invalid return code is provided
    uint64_t delay_duration = static_cast<uint64_t>(json_config.getInteger("delay_duration", 0));
    uint64_t delay_enabled = static_cast<uint64_t>(json_config.getInteger("delay_enabled", 0));
    uint64_t abort_code = static_cast<uint64_t>(json_config.getInteger("abort_code", 0));
    uint64_t abort_enabled = static_cast<uint64_t>(json_config.getInteger("abort_enabled", 0));

    if (delay_enabled > 0) {
      if (delay_enabled > 100) {
        throw EnvoyException("delay_enabled cannot be greater than 100");
      }
      if (0 == delay_duration) {
        throw EnvoyException("delay duration cannot be 0 when delay_enabled is greater than 1");
      }
    }

    if (abort_enabled > 0) {
      if (abort_enabled > 100) {
        throw EnvoyException("abort_enabled cannot be greater than 100");
      }
    }

    std::vector<Router::ConfigUtility::HeaderData> fault_filter_headers;
    if (json_config.hasObject("headers")) {
      std::vector<Json::Object> config_headers = json_config.getObjectArray("headers");
      for (const Json::Object& header_map : config_headers) {
        // allow header value to be empty, allows matching to be only based on header presence.
        fault_filter_headers.emplace_back(Http::LowerCaseString(header_map.getString("name")),
                                          header_map.getString("value", EMPTY_STRING));
      }
    }

    Http::FaultFilterConfigPtr config(new Http::FaultFilterConfig(
        abort_enabled, abort_code, delay_enabled, delay_duration, fault_filter_headers,
        server.runtime(), stats_prefix, server.stats()));
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
