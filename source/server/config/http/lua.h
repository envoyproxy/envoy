#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/http/lua.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the Lua filter. @see NamedHttpFilterConfigFactory.
 */
class LuaFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;

  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stat_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::http::Lua()};
  }

  std::string name() override { return Config::HttpFilterNames::get().LUA; }

private:
  HttpFilterFactoryCb createFilter(const envoy::api::v2::filter::http::Lua& proto_config,
                                   const std::string&, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
