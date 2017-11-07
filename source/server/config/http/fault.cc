#include "server/config/http/fault.h"

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/fault_filter.h"

#include "api/filter/http/fault.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
FaultFilterConfig::createFaultFilter(const envoy::api::v2::filter::http::HTTPFault& fault,
                                     const std::string& stats_prefix, FactoryContext& context) {
  Http::FaultFilterConfigSharedPtr config(
      new Http::FaultFilterConfig(fault, context.runtime(), stats_prefix, context.scope()));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::FaultFilter(config)});
  };
}

HttpFilterFactoryCb FaultFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                           const std::string& stats_prefix,
                                                           FactoryContext& context) {
  envoy::api::v2::filter::http::HTTPFault fault;
  Config::FilterJson::translateFaultFilter(json_config, fault);
  return createFaultFilter(fault, stats_prefix, context);
}

HttpFilterFactoryCb FaultFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& config,
                                                                    const std::string& stats_prefix,
                                                                    FactoryContext& context) {
  return createFaultFilter(dynamic_cast<const envoy::api::v2::filter::http::HTTPFault&>(config),
                           stats_prefix, context);
}

/**
 * Static registration for the fault filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<FaultFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
