#pragma once

#include "envoy/extensions/router/cluster_specifiers/lua/v3/lua.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/common/lua/wrappers.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Lua {

using LuaClusterSpecifierConfigProto =
    envoy::extensions::router::cluster_specifiers::lua::v3::LuaConfig;

class PerLuaCodeSetup : Logger::Loggable<Logger::Id::lua> {
public:
  PerLuaCodeSetup(const std::string& lua_code, ThreadLocal::SlotAllocator& tls);

  Extensions::Filters::Common::Lua::CoroutinePtr createCoroutine() {
    return lua_state_.createCoroutine();
  }

  int clusterFunctionRef() { return lua_state_.getGlobalRef(cluster_function_slot_); }

private:
  uint64_t cluster_function_slot_{};

  Filters::Common::Lua::ThreadLocalState lua_state_;
};

using PerLuaCodeSetupPtr = std::unique_ptr<PerLuaCodeSetup>;

class HeaderMapWrapper : public Filters::Common::Lua::BaseLuaObject<HeaderMapWrapper> {
public:
  HeaderMapWrapper(const Http::HeaderMap& headers) : headers_(headers) {}

  static ExportedFunctions exportedFunctions() { return {{"get", static_luaGet}}; }

private:
  /**
   * Get a header value from the map.
   * @param 1 (string): header name.
   * @return string value if found or nil.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaGet);

  const Http::HeaderMap& headers_;
};

using HeaderMapRef = Filters::Common::Lua::LuaDeathRef<HeaderMapWrapper>;

class RouteHandleWrapper : public Filters::Common::Lua::BaseLuaObject<RouteHandleWrapper> {
public:
  RouteHandleWrapper(const Http::HeaderMap& headers) : headers_(headers) {}

  static ExportedFunctions exportedFunctions() { return {{"headers", static_luaHeaders}}; }

private:
  /**
   * @return a handle to the headers.
   */
  DECLARE_LUA_FUNCTION(RouteHandleWrapper, luaHeaders);

  const Http::HeaderMap& headers_;
  HeaderMapRef headers_wrapper_;
};

using RouteHandleRef = Filters::Common::Lua::LuaDeathRef<RouteHandleWrapper>;

class LuaClusterSpecifierConfig : Logger::Loggable<Logger::Id::lua> {
public:
  LuaClusterSpecifierConfig(const LuaClusterSpecifierConfigProto& config,
                            Server::Configuration::CommonFactoryContext& context);

  PerLuaCodeSetup* perLuaCodeSetup() const { return per_lua_code_setup_ptr_.get(); }
  const std::string& defaultCluster() const { return default_cluster_; }

private:
  PerLuaCodeSetupPtr per_lua_code_setup_ptr_;
  const std::string default_cluster_;
};

using LuaClusterSpecifierConfigSharedPtr = std::shared_ptr<LuaClusterSpecifierConfig>;

class LuaClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                                  Logger::Loggable<Logger::Id::lua> {
public:
  LuaClusterSpecifierPlugin(LuaClusterSpecifierConfigSharedPtr config);
  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& header) const override;

private:
  std::string startLua(const Http::HeaderMap& headers) const;

  LuaClusterSpecifierConfigSharedPtr config_;
  const int function_ref_;
};

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
