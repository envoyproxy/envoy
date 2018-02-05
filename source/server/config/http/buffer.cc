#include "server/config/http/buffer.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/buffer_filter.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb BufferFilterConfig::createFilter(
    const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
    const std::string& stats_prefix, FactoryContext& context) {
  ASSERT(proto_config.has_max_request_bytes());
  ASSERT(proto_config.has_max_request_time());

  Http::BufferFilterConfigConstSharedPtr filter_config(new Http::BufferFilterConfig{
      Http::BufferFilter::generateStats(stats_prefix, context.scope()),
      static_cast<uint64_t>(proto_config.max_request_bytes().value()),
      std::chrono::seconds(PROTOBUF_GET_SECONDS_REQUIRED(proto_config, max_request_time))});
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::BufferFilter(filter_config)});
  };
}

HttpFilterFactoryCb BufferFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& stats_prefix,
                                                            FactoryContext& context) {
  envoy::config::filter::http::buffer::v2::Buffer proto_config;
  Config::FilterJson::translateBufferFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

HttpFilterFactoryCb
BufferFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                 const std::string& stats_prefix,
                                                 FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::buffer::v2::Buffer&>(
          proto_config),
      stats_prefix, context);
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<BufferFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
