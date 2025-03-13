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
    : public Common::DualFactoryBase<envoy::extensions::filters::http::lua::v3::Lua,
                                     envoy::extensions::filters::http::lua::v3::LuaPerRoute> {
public:
  LuaFilterConfig() : DualFactoryBase("envoy.filters.http.lua") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
      const std::string& stats_prefix, DualInfo info,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::lua::v3::LuaPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

using UpstreamLuaFilterConfig = LuaFilterConfig;

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
