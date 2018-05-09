#pragma once

#include <string>

#include "envoy/config/filter/http/lua/v2/lua.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

/**
 * Config registration for the Lua filter. @see NamedHttpFilterConfigFactory.
 */
class LuaFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::lua::v2::Lua()};
  }

  std::string name() override { return HttpFilterNames::get().LUA; }

private:
  Http::FilterFactoryCb createFilter(const envoy::config::filter::http::lua::v2::Lua& proto_config,
                                     const std::string&,
                                     Server::Configuration::FactoryContext& context);
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
