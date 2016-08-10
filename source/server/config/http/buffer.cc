#include "envoy/server/instance.h"

#include "common/http/filter/buffer_filter.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the buffer filter. @see HttpFilterConfigFactory.
 */
class BufferFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& json_config,
                                             const std::string& stats_prefix,
                                             Server::Instance& server) override {
    if (type != HttpFilterType::Decoder || name != "buffer") {
      return nullptr;
    }

    Http::BufferFilterConfigPtr config(new Http::BufferFilterConfig{
        Http::BufferFilter::generateStats(stats_prefix, server.stats()),
        static_cast<uint64_t>(json_config.getInteger("max_request_bytes")),
        std::chrono::seconds(json_config.getInteger("max_request_time_s"))});
    return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(
          Http::StreamDecoderFilterPtr{new Http::BufferFilter(config)});
    };
  }
};

/**
 * Static registration for the buffer filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<BufferFilterConfig> register_;

} // Configuration
} // Server
