#include "source/extensions/filters/http/on_demand/config.h"

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"

#include "source/extensions/filters/http/on_demand/on_demand_update.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

Http::FilterFactoryCb OnDemandFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand&, const std::string&,
    Server::Configuration::FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<Extensions::HttpFilters::OnDemand::OnDemandRouteUpdate>());
  };
}

/**
 * Static registration for the on-demand filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OnDemandFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
