#include "source/extensions/router/cluster_specifiers/lua/lua_cluster_specifier.h"

#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Lua {

PerLuaCodeSetup::PerLuaCodeSetup(const std::string& lua_code, ThreadLocal::SlotAllocator& tls)
    : lua_state_(lua_code, tls) {
  lua_state_.registerType<HeaderMapWrapper>();
  lua_state_.registerType<RouteHandleWrapper>();

  const Filters::Common::Lua::InitializerList initializers;

  cluster_function_slot_ = lua_state_.registerGlobal("envoy_on_route", initializers);
  if (lua_state_.getGlobalRef(cluster_function_slot_) == LUA_REFNIL) {
    throw EnvoyException(
        "envoy_on_route() function not found. Lua will not hook cluster specifier.");
  }
}

int HeaderMapWrapper::luaGet(lua_State* state) {
  absl::string_view key = Filters::Common::Lua::getStringViewFromLuaString(state, 2);
  const Envoy::Http::HeaderUtility::GetAllOfHeaderAsStringResult value =
      Envoy::Http::HeaderUtility::getAllOfHeaderAsString(headers_,
                                                         Envoy::Http::LowerCaseString(key));
  if (value.result().has_value()) {
    lua_pushlstring(state, value.result().value().data(), value.result().value().size());
    return 1;
  } else {
    return 0;
  }
}

int RouteHandleWrapper::luaHeaders(lua_State* state) {
  if (headers_wrapper_.get() != nullptr) {
    headers_wrapper_.pushStack();
  } else {
    headers_wrapper_.reset(HeaderMapWrapper::create(state, headers_), true);
  }
  return 1;
}

LuaClusterSpecifierConfig::LuaClusterSpecifierConfig(
    const LuaClusterSpecifierConfigProto& config,
    Server::Configuration::CommonFactoryContext& context)
    : main_thread_dispatcher_(context.mainThreadDispatcher()),
      default_cluster_(config.default_cluster()) {
  const std::string code_str = THROW_OR_RETURN_VALUE(
      Config::DataSource::read(config.source_code(), true, context.api()), std::string);
  per_lua_code_setup_ptr_ = std::make_unique<PerLuaCodeSetup>(code_str, context.threadLocal());
}

LuaClusterSpecifierPlugin::LuaClusterSpecifierPlugin(LuaClusterSpecifierConfigSharedPtr config)
    : config_(config),
      function_ref_(config_->perLuaCodeSetup() ? config_->perLuaCodeSetup()->clusterFunctionRef()
                                               : LUA_REFNIL) {}

std::string LuaClusterSpecifierPlugin::startLua(const Http::HeaderMap& headers) const {
  if (function_ref_ == LUA_REFNIL) {
    return config_->defaultCluster();
  }
  Filters::Common::Lua::CoroutinePtr coroutine = config_->perLuaCodeSetup()->createCoroutine();

  RouteHandleRef handle;
  handle.reset(RouteHandleWrapper::create(coroutine->luaState(), headers), true);

  TRY_NEEDS_AUDIT {
    coroutine->start(function_ref_, 1, []() {});
  }
  END_TRY catch (const Filters::Common::Lua::LuaException& e) {
    ENVOY_LOG(error, "script log: {}, use default cluster", e.what());
    return config_->defaultCluster();
  }
  if (!lua_isstring(coroutine->luaState(), -1)) {
    ENVOY_LOG(error, "script log: return value is not string, use default cluster");
    return config_->defaultCluster();
  }
  return std::string(Filters::Common::Lua::getStringViewFromLuaString(coroutine->luaState(), -1));
}

Envoy::Router::RouteConstSharedPtr
LuaClusterSpecifierPlugin::route(Envoy::Router::RouteConstSharedPtr parent,
                                 const Http::RequestHeaderMap& headers) const {
  return std::make_shared<Envoy::Router::RouteEntryImplBase::DynamicRouteEntry>(
      dynamic_cast<const Envoy::Router::RouteEntryImplBase*>(parent.get()), parent,
      startLua(headers));
}
} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
