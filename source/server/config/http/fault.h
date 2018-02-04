#pragma once

#include <string>

#include "envoy/config/filter/http/fault/v2/fault.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

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
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::fault::v2::HTTPFault()};
  }

  std::string name() override { return Config::HttpFilterNames::get().FAULT; }

private:
  HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::fault::v2::HTTPFault& proto_config,
               const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
