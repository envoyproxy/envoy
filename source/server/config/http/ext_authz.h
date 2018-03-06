#pragma once

#include <string>

#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the external authorization filter. @see NamedHttpFilterConfigFactory.
 */
class ExtAuthzFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config, const std::string&,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::ext_authz::v2::ExtAuthz()};
  }

  std::string name() override { return Config::HttpFilterNames::get().EXT_AUTHORIZATION; }

private:
  HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::ext_authz::v2::ExtAuthz& proto_config,
               const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
