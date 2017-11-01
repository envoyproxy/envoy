#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/http/fault.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the fault injection filter. @see NamedHttpFilterConfigFactory.
 */
class FaultFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                   const std::string& stats_prefix,

                                                   FactoryContext& context) override;
  std::string name() override { return Config::HttpFilterNames::get().FAULT; }

private:
  HttpFilterFactoryCb createFaultFilter(const envoy::api::v2::filter::http::HTTPFault& fault,
                                        const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
