#include "server/config/http/buffer.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "common/http/filter/buffer_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb BufferFilterConfig::createFilterFactory(HttpFilterType type,
                                                            const Json::Object& json_config,
                                                            const std::string& stats_prefix,
                                                            Server::Instance& server) {
  if (type != HttpFilterType::Decoder) {
    throw EnvoyException(
        fmt::format("{} http filter must be configured as a decoder filter.", name()));
  }

  json_config.validateSchema(Json::Schema::BUFFER_HTTP_FILTER_SCHEMA);

  Http::BufferFilterConfigConstSharedPtr config(new Http::BufferFilterConfig{
      Http::BufferFilter::generateStats(stats_prefix, server.stats()),
      static_cast<uint64_t>(json_config.getInteger("max_request_bytes")),
      std::chrono::seconds(json_config.getInteger("max_request_time_s"))});
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::BufferFilter(config)});
  };
}

std::string BufferFilterConfig::name() { return "buffer"; }

/**
 * Static registration for the buffer filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<BufferFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
