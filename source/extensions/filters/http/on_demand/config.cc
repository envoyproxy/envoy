#include "source/extensions/filters/http/on_demand/config.h"

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"

#include "source/extensions/filters/http/on_demand/on_demand_update.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

Http::FilterFactoryCb OnDemandFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  OnDemandFilterConfigSharedPtr config = std::make_shared<OnDemandFilterConfig>(
      proto_config, context.serverFactoryContext().clusterManager(),
      context.messageValidationVisitor());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<OnDemandRouteUpdate>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
OnDemandFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  return std::make_shared<const OnDemandFilterConfig>(proto_config, context.clusterManager(),
                                                      validation_visitor);
}

/**
 * Static registration for the on-demand filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OnDemandFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
