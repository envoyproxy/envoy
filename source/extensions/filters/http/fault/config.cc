#include "extensions/filters/http/fault/config.h"

#include "envoy/config/filter/http/fault/v2/fault.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/fault/fault_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

Http::FilterFactoryCb FaultFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::fault::v2::HTTPFault& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  FaultFilterConfigSharedPtr filter_config(new FaultFilterConfig(
      config, context.runtime(), stats_prefix, context.scope(), context.random()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<FaultFilter>(filter_config));
  };
}

Http::FilterFactoryCb
FaultFilterFactory::createFilterFactory(const Json::Object& json_config,
                                        const std::string& stats_prefix,
                                        Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::fault::v2::HTTPFault proto_config;
  Config::FilterJson::translateFaultFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, stats_prefix, context);
}

Router::RouteSpecificFilterConfigConstSharedPtr
FaultFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::fault::v2::HTTPFault& config,
    Server::Configuration::FactoryContext&) {
  return std::make_shared<const Fault::FaultSettings>(config);
}

/**
 * Static registration for the fault filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<FaultFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
