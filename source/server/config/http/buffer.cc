#include "server/config/http/buffer.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/buffer_filter.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
BufferFilterConfig::createBufferFilter(const envoy::api::v2::filter::http::Buffer& buffer,
                                       const std::string& stats_prefix, FactoryContext& context) {
  ASSERT(buffer.has_max_request_bytes());
  ASSERT(buffer.has_max_request_time());

  Http::BufferFilterConfigConstSharedPtr config(new Http::BufferFilterConfig{
      Http::BufferFilter::generateStats(stats_prefix, context.scope()),
      static_cast<uint64_t>(buffer.max_request_bytes().value()),
      std::chrono::seconds(PROTOBUF_GET_SECONDS_REQUIRED(buffer, max_request_time))});
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::BufferFilter(config)});
  };
}

HttpFilterFactoryCb BufferFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stats_prefix,
                                                            FactoryContext& context) {
  envoy::api::v2::filter::http::Buffer buffer;
  Config::FilterJson::translateBufferFilter(json_config, buffer);
  return createBufferFilter(buffer, stats_prefix, context);
}

HttpFilterFactoryCb BufferFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string& stats_prefix, FactoryContext& context) {
  return createBufferFilter(dynamic_cast<const envoy::api::v2::filter::http::Buffer&>(config),
                            stats_prefix, context);
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<BufferFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
