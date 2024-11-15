#include "source/extensions/filters/http/lua/config.h"

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/lua/lua_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

Http::FilterFactoryCb LuaFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();

  FilterConfigConstSharedPtr filter_config(
      new FilterConfig{proto_config, server_context.threadLocal(), server_context.clusterManager(),
                       server_context.api(), context.scope(), stat_prefix});
  auto& time_source = server_context.mainThreadDispatcher().timeSource();
  return [filter_config, &time_source](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config, time_source));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
LuaFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::lua::v3::LuaPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config, context);
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(LuaFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.lua");

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
