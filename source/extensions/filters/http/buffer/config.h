#pragma once

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

/**
 * Config registration for the buffer filter. @see NamedHttpFilterConfigFactory.
 */
class BufferFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::buffer::v2::Buffer> {
public:
  BufferFilterFactory() : FactoryBase(HttpFilterNames::get().BUFFER) {}

  Server::Configuration::HttpFilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

private:
  Server::Configuration::HttpFilterFactoryCb createTypedFilterFactoryFromProto(
      const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
