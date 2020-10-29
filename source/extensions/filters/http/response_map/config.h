#pragma once

#include "envoy/extensions/filters/http/response_map/v3/response_map.pb.h"
#include "envoy/extensions/filters/http/response_map/v3/response_map.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ResponseMapFilter {

/**
 * Config registration for the response_map filter. @see NamedHttpFilterConfigFactory.
 */
class ResponseMapFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::response_map::v3::ResponseMap,
          envoy::extensions::filters::http::response_map::v3::ResponseMapPerRoute> {
public:
  ResponseMapFilterFactory() : FactoryBase(HttpFilterNames::get().ResponseMap) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::response_map::v3::ResponseMap& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

private:
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::response_map::v3::ResponseMapPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace ResponseMapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
