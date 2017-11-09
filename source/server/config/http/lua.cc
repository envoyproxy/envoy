#include "server/config/http/lua.h"

#include "envoy/registry/registry.h"

#include "common/config/json_utility.h"
#include "common/http/filter/lua/lua_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

namespace {

HttpFilterFactoryCb createLuaFilterFactory(const envoy::api::v2::filter::http::Lua& lua,
                                           const std::string&, FactoryContext& context) {
  Http::Filter::Lua::FilterConfigConstSharedPtr config(new Http::Filter::Lua::FilterConfig{
      lua.inline_code(), context.threadLocal(), context.clusterManager()});
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Http::Filter::Lua::Filter>(config));
  };
}

} // namespace

HttpFilterFactoryCb LuaFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                         const std::string& stat_prefix,
                                                         FactoryContext& context) {
  json_config.validateSchema(Json::Schema::LUA_HTTP_FILTER_SCHEMA);
  envoy::api::v2::filter::http::Lua lua;
  JSON_UTIL_SET_STRING(json_config, lua, inline_code);
  return createLuaFilterFactory(lua, stat_prefix, context);
}

HttpFilterFactoryCb LuaFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& config,
                                                                  const std::string& stat_prefix,
                                                                  FactoryContext& context) {
  return createLuaFilterFactory(dynamic_cast<const envoy::api::v2::filter::http::Lua&>(config),
                                stat_prefix, context);
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<LuaFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
