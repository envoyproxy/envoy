#pragma once

#include <regex>
#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/http/squash.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the squash filter. @see NamedHttpFilterConfigFactory.
 */
class SquashFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config, const std::string&,
                                          FactoryContext& context) override;

  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string&,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::http::Squash()};
  }

  std::string name() override { return Config::HttpFilterNames::get().SQUASH; }

private:
  HttpFilterFactoryCb createFilter(const envoy::api::v2::filter::http::Squash& proto_config,
                                   FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
