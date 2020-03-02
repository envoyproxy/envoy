#include "extensions/filters/http/fault/config.h"

#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/fault/fault_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

Http::FilterFactoryCb FaultFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::fault::v3::HTTPFault& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  FaultFilterConfigSharedPtr filter_config(new FaultFilterConfig(
      config, context.runtime(), stats_prefix, context.scope(), context.timeSource()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<FaultFilter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
FaultFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::fault::v3::HTTPFault& config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const Fault::FaultSettings>(config);
}

/**
 * Static registration for the fault filter. @see RegisterFactory.
 */
REGISTER_FACTORY(FaultFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.fault"};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
