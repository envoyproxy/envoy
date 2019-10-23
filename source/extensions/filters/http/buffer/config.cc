#include "extensions/filters/http/buffer/config.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/buffer/buffer_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

Http::FilterFactoryCb BufferFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::buffer::v2::Buffer& proto_config, const std::string&,
    Server::Configuration::FactoryContext&) {
  ASSERT(proto_config.has_max_request_bytes());

  BufferFilterConfigSharedPtr filter_config(new BufferFilterConfig(proto_config));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BufferFilter>(filter_config));
  };
}

Http::FilterFactoryCb
BufferFilterFactory::createFilterFactory(const Json::Object& json_config,
                                         const std::string& stats_prefix,
                                         Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::buffer::v2::Buffer proto_config;
  Config::FilterJson::translateBufferFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, stats_prefix, context);
}

Router::RouteSpecificFilterConfigConstSharedPtr
BufferFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::buffer::v2::BufferPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const BufferFilterSettings>(proto_config);
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
REGISTER_FACTORY(BufferFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
