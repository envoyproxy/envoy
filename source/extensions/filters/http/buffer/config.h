#pragma once

#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

/**
 * Config registration for the buffer filter.
 */
class BufferFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::buffer::v3::Buffer,
                                 envoy::extensions::filters::http::buffer::v3::BufferPerRoute> {
public:
  BufferFilterFactory() : FactoryBase("envoy.filters.http.buffer") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::buffer::v3::Buffer& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::buffer::v3::BufferPerRoute&,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

DECLARE_FACTORY(BufferFilterFactory);

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
