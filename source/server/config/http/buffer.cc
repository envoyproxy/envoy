#include "server/config/http/buffer.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/registry/registry.h"

#include "common/http/filter/buffer_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb BufferFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stats_prefix,
                                                            FactoryContext& context) {
  json_config.validateSchema(Json::Schema::BUFFER_HTTP_FILTER_SCHEMA);

  Http::BufferFilterConfigConstSharedPtr config(new Http::BufferFilterConfig{
      Http::BufferFilter::generateStats(stats_prefix, context.scope()),
      static_cast<uint64_t>(json_config.getInteger("max_request_bytes")),
      std::chrono::seconds(json_config.getInteger("max_request_time_s"))});
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::BufferFilter(config)});
  };
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<BufferFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
