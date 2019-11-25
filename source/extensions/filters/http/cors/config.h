#pragma once

#include "envoy/config/filter/http/cors/v2/cors.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

/**
 * Config registration for the cors filter. @see NamedHttpFilterConfigFactory.
 */
class CorsFilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return std::make_unique<envoy::config::filter::http::cors::v2::PerRouteCorsPolicy>();
  }

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                  Server::Configuration::ServerFactoryContext& context,
                                  ProtobufMessage::ValidationVisitor& validator) override;

  std::string name() override { return HttpFilterNames::get().Cors; }
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
