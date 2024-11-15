#pragma once

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

/**
 * Config registration for the OnDemand filter. @see NamedHttpFilterConfigFactory.
 */
class OnDemandFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::on_demand::v3::OnDemand,
                                 envoy::extensions::filters::http::on_demand::v3::PerRouteConfig> {
public:
  OnDemandFilterFactory() : FactoryBase(HttpFilterNames::get().OnDemand) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& visitor) override;
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
