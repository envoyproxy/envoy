#include "extensions/filters/http/lua/config.h"

#include "envoy/config/filter/http/lua/v2/lua.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/lua/lua_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

Server::Configuration::HttpFilterFactoryCb
LuaFilterConfig::createFilter(const envoy::config::filter::http::lua::v2::Lua& proto_config,
                              const std::string&, Server::Configuration::FactoryContext& context) {
  FilterConfigConstSharedPtr filter_config(new FilterConfig{
      proto_config.inline_code(), context.threadLocal(), context.clusterManager()});
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

Server::Configuration::HttpFilterFactoryCb
LuaFilterConfig::createFilterFactory(const Json::Object& json_config,
                                     const std::string& stat_prefix,
                                     Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::lua::v2::Lua proto_config;
  Config::FilterJson::translateLuaFilter(json_config, proto_config);
  return createFilter(proto_config, stat_prefix, context);
}

Server::Configuration::HttpFilterFactoryCb
LuaFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                              const std::string& stat_prefix,
                                              Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::lua::v2::Lua&>(
          proto_config),
      stat_prefix, context);
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<LuaFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
