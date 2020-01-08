#pragma once

#include "envoy/extensions/filters/http/buffer/v3alpha/buffer.pb.h"
#include "envoy/extensions/filters/http/buffer/v3alpha/buffer.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

/**
 * Config registration for the buffer filter.
 */
class BufferFilterFactory : public Common::FactoryBase<
                                envoy::extensions::filters::http::buffer::v3alpha::Buffer,
                                envoy::extensions::filters::http::buffer::v3alpha::BufferPerRoute> {
public:
  BufferFilterFactory() : FactoryBase(HttpFilterNames::get().Buffer) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::buffer::v3alpha::Buffer& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::buffer::v3alpha::BufferPerRoute&,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
