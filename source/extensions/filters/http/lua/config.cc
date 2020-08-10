#include "extensions/filters/http/lua/config.h"

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/lua/lua_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

Http::FilterFactoryCb LuaFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::lua::v3::Lua& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  FilterConfigConstSharedPtr filter_config(new FilterConfig{
      proto_config, context.threadLocal(), context.clusterManager(), context.api()});
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
LuaFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::lua::v3::LuaPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config, context.threadLocal(), context.api());
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LuaFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory){"envoy.lua"};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
