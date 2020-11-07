#include "extensions/filters/http/on_demand/config.h"

#include "envoy/config/filter/http/on_demand/v2/on_demand.pb.validate.h"

#include "extensions/filters/http/on_demand/on_demand_update.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

Http::FilterFactoryCb OnDemandFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::on_demand::v2::OnDemand&, const std::string&,
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
