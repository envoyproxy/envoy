#pragma once

#include "envoy/extensions/router/cluster_specifiers/lua/v3/lua.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/config/datasource.h"
#include "source/common/runtime/runtime_features.h"
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

  void runtimeGC() { lua_state_.runtimeGC(); }

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

class ClusterWrapper : public Filters::Common::Lua::BaseLuaObject<ClusterWrapper> {
public:
  ClusterWrapper(Upstream::ClusterInfoConstSharedPtr cluster) : cluster_(cluster) {}

  static ExportedFunctions exportedFunctions() {
    return {
        {"numConnections", static_luaNumConnections},
        {"numRequests", static_luaNumRequests},
        {"numPendingRequests", static_luaNumPendingRequests},
    };
  }

  void onMarkDead() override { cluster_.reset(); }

private:
  DECLARE_LUA_FUNCTION(ClusterWrapper, luaNumConnections);
  DECLARE_LUA_FUNCTION(ClusterWrapper, luaNumRequests);
  DECLARE_LUA_FUNCTION(ClusterWrapper, luaNumPendingRequests);

  Upstream::ClusterInfoConstSharedPtr cluster_;
};

using ClusterRef = Filters::Common::Lua::LuaDeathRef<ClusterWrapper>;

class RouteHandleWrapper : public Filters::Common::Lua::BaseLuaObject<RouteHandleWrapper> {
public:
  RouteHandleWrapper(const Http::HeaderMap& headers, Upstream::ClusterManager& cm)
      : headers_(headers), cm_(cm) {}

  static ExportedFunctions exportedFunctions() {
    return {
        {"headers", static_luaHeaders},
        {"getCluster", static_luaGetCluster},
    };
  }

  // All embedded references should be reset when the object is marked dead. This is to ensure that
  // we won't do the resetting in the destructor, which may be called after the referenced
  // coroutine's lua_State is closed. And if that happens, the resetting will cause a crash.
  void onMarkDead() override {
    headers_wrapper_.reset();
    clusters_.clear();
  }

private:
  /**
   * @return a handle to the headers.
   */
  DECLARE_LUA_FUNCTION(RouteHandleWrapper, luaHeaders);
  DECLARE_LUA_FUNCTION(RouteHandleWrapper, luaGetCluster);

  const Http::HeaderMap& headers_;
  Upstream::ClusterManager& cm_;
  HeaderMapRef headers_wrapper_;
  std::vector<ClusterRef> clusters_;
};

using RouteHandleRef = Filters::Common::Lua::LuaDeathRef<RouteHandleWrapper>;

class LuaClusterSpecifierConfig : Logger::Loggable<Logger::Id::lua> {
public:
  LuaClusterSpecifierConfig(const LuaClusterSpecifierConfigProto& config,
                            Server::Configuration::CommonFactoryContext& context);

  PerLuaCodeSetup* perLuaCodeSetup() const { return per_lua_code_setup_ptr_.get(); }
  const std::string& defaultCluster() const { return default_cluster_; }
  Upstream::ClusterManager& clusterManager() { return cm_; }

private:
  Upstream::ClusterManager& cm_;
  PerLuaCodeSetupPtr per_lua_code_setup_ptr_;
  const std::string default_cluster_;
};

using LuaClusterSpecifierConfigSharedPtr = std::shared_ptr<LuaClusterSpecifierConfig>;

class LuaClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                                  Logger::Loggable<Logger::Id::lua> {
public:
  LuaClusterSpecifierPlugin(LuaClusterSpecifierConfigSharedPtr config);
  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& header,
                                           const StreamInfo::StreamInfo&, uint64_t) const override;

private:
  std::string startLua(const Http::HeaderMap& headers) const;

  LuaClusterSpecifierConfigSharedPtr config_;
  const int function_ref_;
};

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
