#include "server/config/http/fault.h"

#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/fault_filter.h"

#include "api/filter/http/fault.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
FaultFilterConfig::createFilter(const envoy::api::v2::filter::http::HTTPFault& config,
                                const std::string& stats_prefix, FactoryContext& context) {
  Http::FaultFilterConfigSharedPtr filter_config(
      new Http::FaultFilterConfig(config, context.runtime(), stats_prefix, context.scope()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::FaultFilter(filter_config)});
  };
}

HttpFilterFactoryCb FaultFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                           const std::string& stats_prefix,
                                                           FactoryContext& context) {
  envoy::api::v2::filter::http::HTTPFault proto_config;
  Config::FilterJson::translateFaultFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

HttpFilterFactoryCb
FaultFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                const std::string& stats_prefix,
                                                FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::api::v2::filter::http::HTTPFault&>(
          proto_config),
      stats_prefix, context);
}

/**
 * Static registration for the fault filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<FaultFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
