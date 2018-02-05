#include "server/config/http/lua.h"

#include "envoy/config/filter/http/lua/v2/lua.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/lua/lua_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
LuaFilterConfig::createFilter(const envoy::config::filter::http::lua::v2::Lua& proto_config,
                              const std::string&, FactoryContext& context) {
  Http::Filter::Lua::FilterConfigConstSharedPtr filter_config(new Http::Filter::Lua::FilterConfig{
      proto_config.inline_code(), context.threadLocal(), context.clusterManager()});
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Http::Filter::Lua::Filter>(filter_config));
  };
}

HttpFilterFactoryCb LuaFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                         const std::string& stat_prefix,
                                                         FactoryContext& context) {
  envoy::config::filter::http::lua::v2::Lua proto_config;
  Config::FilterJson::translateLuaFilter(json_config, proto_config);
  return createFilter(proto_config, stat_prefix, context);
}

HttpFilterFactoryCb
LuaFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                              const std::string& stat_prefix,
                                              FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::lua::v2::Lua&>(
          proto_config),
      stat_prefix, context);
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<LuaFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
