#pragma once

#include "envoy/http/header_map.h"

#include "common/lua/lua.h"

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

class HeaderMapWrapper;
class MetadataMapWrapper;

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
            {"replace", static_luaReplace},
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

  /**
   * Replace a header in the map. If the header does not exist, it will be added.
   * @param 1 (string): header name.
   * @param 2 (string): header value.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaReplace);

  void checkModifiable(lua_State* state);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  HeaderMap& headers_;
  CheckModifiableCb cb_;
  Envoy::Lua::LuaDeathRef<HeaderMapIterator> iterator_;

  friend class HeaderMapIterator;
};

/**
 * Lua wrapper for a metadata map.
 * TODO(dio): define MetadataMapIterator.
 */
class MetadataMapWrapper : public Envoy::Lua::BaseLuaObject<MetadataMapWrapper> {
public:
  MetadataMapWrapper(const envoy::api::v2::core::Metadata& metadata) : metadata_{metadata} {}

  static ExportedFunctions exportedFunctions() { return {{"get", static_luaGet}}; }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter.
   * @param 2 (string): key.
   * @return string value if found or nil.
   */
  DECLARE_LUA_FUNCTION(MetadataMapWrapper, luaGet);

  void setValue(lua_State* state, const ProtobufWkt::Value&& value);
  void createTable(lua_State* state,
                   const ProtobufWkt::Map<std::string, ProtobufWkt::Value>&& fields);

  const envoy::api::v2::core::Metadata metadata_;
};

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
