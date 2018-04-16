#include "extensions/filters/http/fault/config.h"

#include "envoy/config/filter/http/fault/v2/fault.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/fault/fault_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

Server::Configuration::HttpFilterFactoryCb
FaultFilterFactory::createFilter(const envoy::config::filter::http::fault::v2::HTTPFault& config,
                                 const std::string& stats_prefix,
                                 Server::Configuration::FactoryContext& context) {
  FaultFilterConfigSharedPtr filter_config(
      new FaultFilterConfig(config, context.runtime(), stats_prefix, context.scope()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<FaultFilter>(filter_config));
  };
}

Server::Configuration::HttpFilterFactoryCb
FaultFilterFactory::createFilterFactory(const Json::Object& json_config,
                                        const std::string& stats_prefix,
                                        Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::fault::v2::HTTPFault proto_config;
  Config::FilterJson::translateFaultFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

Server::Configuration::HttpFilterFactoryCb
FaultFilterFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                 const std::string& stats_prefix,
                                                 Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::fault::v2::HTTPFault&>(
          proto_config),
      stats_prefix, context);
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
