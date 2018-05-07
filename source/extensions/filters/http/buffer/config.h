#pragma once

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

/**
 * Config registration for the buffer filter.
 */
class BufferFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::buffer::v2::Buffer> {
public:
  BufferFilterFactory() : FactoryBase(HttpFilterNames::get().BUFFER) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

  /*ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::buffer::v2::BufferPerRoute()};
  }

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                  Server::Configuration::FactoryContext&) override;*/

private:
  Http::FilterFactoryCb createTypedFilterFactoryFromProto(
      const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
