#include "extensions/filters/http/buffer/config.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/buffer/buffer_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

Server::Configuration::HttpFilterFactoryCb BufferFilterConfigFactory::createFilter(
    const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  ASSERT(proto_config.has_max_request_bytes());
  ASSERT(proto_config.has_max_request_time());

  BufferFilterConfigSharedPtr filter_config(
      new BufferFilterConfig(proto_config, stats_prefix, context.scope()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BufferFilter>(filter_config));
  };
}

Server::Configuration::HttpFilterFactoryCb
BufferFilterConfigFactory::createFilterFactory(const Json::Object& json_config,
                                               const std::string& stats_prefix,
                                               Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::buffer::v2::Buffer proto_config;
  Config::FilterJson::translateBufferFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

Server::Configuration::HttpFilterFactoryCb BufferFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::buffer::v2::Buffer&>(
          proto_config),
      stats_prefix, context);
}

Router::RouteSpecificFilterConfigConstSharedPtr
BufferFilterConfigFactory::createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                                           Server::Configuration::FactoryContext&) {
  return std::make_shared<const BufferFilterSettings>(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::http::buffer::v2::BufferPerRoute&>(proto_config));
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<BufferFilterConfigFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
