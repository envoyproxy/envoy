#pragma once

#include "envoy/http/header_map.h"

#include "common/lua/lua.h"

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

class HeaderMapWrapper;

/**
 * Iterator over a header map.
 */
class HeaderMapIterator : public Envoy::Lua::BaseLuaObject<HeaderMapIterator> {
public:
  HeaderMapIterator(HeaderMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(HeaderMapIterator, luaPairsIterator);

private:
  HeaderMapWrapper& parent_;
  std::vector<const HeaderEntry*> entries_;
  uint64_t current_{};
};

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
            {"remove", static_luaRemove},
            {"__pairs", static_luaPairs}};
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
   * Implementation of the __pairs metamethod so a headers wrapper can be iterated over using
   * pairs().
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaPairs);

  /**
   * Remove a header from the map.
   * @param 1 (string): header name.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaRemove);

  void checkModifiable(lua_State* state);

  HeaderMap& headers_;
  CheckModifiableCb cb_;
  bool iterating_{};

  friend class HeaderMapIterator;
};

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
