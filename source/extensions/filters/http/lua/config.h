#pragma once

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

/**
 * Config registration for the Lua filter. @see NamedHttpFilterConfigFactory.
 */
class LuaFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::lua::v3::Lua,
                                 envoy::extensions::filters::http::lua::v3::LuaPerRoute> {
public:
  LuaFilterConfig() : FactoryBase("envoy.filters.http.lua") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::lua::v3::LuaPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
