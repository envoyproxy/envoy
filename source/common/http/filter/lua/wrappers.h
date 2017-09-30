#pragma once

#include "envoy/http/header_map.h"

#include "common/lua/lua.h"

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

/**
 * Lua wrapper for a header map. Methods that will modify the map will call a check function
 * to see if modification is allowed.
 */
class HeaderMapWrapper : public Envoy::Lua::BaseLuaObject<HeaderMapWrapper> {
public:
  typedef std::function<bool()> CheckModifiableCb;

  HeaderMapWrapper(HeaderMap& headers, CheckModifiableCb cb) : headers_(headers), cb_(cb) {}

  static ExportedFunctions exportedFunctions() {
    return {{"add", static_luaAdd},
            {"get", static_luaGet},
            {"iterate", static_luaIterate},
            {"remove", static_luaRemove}};
  }

private:
  /**
   * Add a header to the map.
   * @param 1 (string): header name.
   * @param 2 (string): header value.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaAdd);

  /**
   * Get a header value from the map.
   * @param 1 (string): header name.
   * @return string value if found or nil.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaGet);

  /**
   * Iterate through all headers.
   * @param 1 (function): callback(key, value) that will be called for each header in the map.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaIterate);

  /**
   * Remove a header from the map.
   * @param 1 (string): header name.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaRemove);

  void checkModifiable(lua_State* state) {
    if (!cb_()) {
      luaL_error(state, "header map can no longer be modified");
    }
  }

  HeaderMap& headers_;
  CheckModifiableCb cb_;
};

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
