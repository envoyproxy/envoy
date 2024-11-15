#include "source/extensions/filters/http/buffer/config.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/buffer/buffer_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

absl::StatusOr<Http::FilterFactoryCb> BufferFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::buffer::v3::Buffer& proto_config, const std::string&,
    DualInfo, Server::Configuration::ServerFactoryContext&) {
  ASSERT(proto_config.has_max_request_bytes());

  BufferFilterConfigSharedPtr filter_config(new BufferFilterConfig(proto_config));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BufferFilter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
BufferFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::buffer::v3::BufferPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const BufferFilterSettings>(proto_config);
}

/**
 * Static registration for the buffer filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(BufferFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.buffer");
LEGACY_REGISTER_FACTORY(UpstreamBufferFilterFactory,
                        Server::Configuration::UpstreamHttpFilterConfigFactory, "envoy.buffer");

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
