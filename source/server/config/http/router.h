#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "api/filter/http/router.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class RouterFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;

  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stat_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::http::Router()};
  }

  std::string name() override { return Config::HttpFilterNames::get().ROUTER; }

private:
  HttpFilterFactoryCb createFilter(const envoy::api::v2::filter::http::Router& proto_config,
                                   const std::string& stat_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
