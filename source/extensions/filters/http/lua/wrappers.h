#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/crypto/utility.h"
#include "source/extensions/filters/common/lua/lua.h"
#include "source/extensions/filters/common/lua/wrappers.h"

#include "openssl/evp.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

class HeaderMapWrapper;

/**
 * Iterator over a header map.
 */
class HeaderMapIterator : public Filters::Common::Lua::BaseLuaObject<HeaderMapIterator> {
public:
  HeaderMapIterator(HeaderMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(HeaderMapIterator, luaPairsIterator);

private:
  HeaderMapWrapper& parent_;
  std::vector<const Http::HeaderEntry*> entries_;
  uint64_t current_{};
};

/**
 * Lua wrapper for a header map. Methods that will modify the map will call a check function
 * to see if modification is allowed.
 */
class HeaderMapWrapper : public Filters::Common::Lua::BaseLuaObject<HeaderMapWrapper> {
public:
  using CheckModifiableCb = std::function<bool()>;

  HeaderMapWrapper(Http::HeaderMap& headers, CheckModifiableCb cb) : headers_(headers), cb_(cb) {}

  static ExportedFunctions exportedFunctions() {
    return {{"add", static_luaAdd},
            {"get", static_luaGet},
            {"getAtIndex", static_luaGetAtIndex},
            {"getNumValues", static_luaGetNumValues},
            {"remove", static_luaRemove},
            {"replace", static_luaReplace},
            {"setHttp1ReasonPhrase", static_luaSetHttp1ReasonPhrase},
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
   * Get a header value from the map.
   * @param 1 (string): header name.
   * @param 2 (int): index of the value for the given header which needs to be retrieved.
   * @return string value if found or nil.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaGetAtIndex);

  /**
   * Get the header value size from the map.
   * @param 1 (string): header name.
   * @return int value size if found or 0.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaGetNumValues);

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

  /**
   * Set a HTTP1 reason phrase
   * @param 1 (string): reason phrase
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaSetHttp1ReasonPhrase);

  void checkModifiable(lua_State* state);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  Http::HeaderMap& headers_;
  CheckModifiableCb cb_;
  Filters::Common::Lua::LuaDeathRef<HeaderMapIterator> iterator_;

  friend class HeaderMapIterator;
};

/**
 * Lua wrapper for key for accessing the imported public keys.
 */
class PublicKeyWrapper : public Filters::Common::Lua::BaseLuaObject<PublicKeyWrapper> {
public:
  explicit PublicKeyWrapper(absl::string_view key) : public_key_(key) {}
  static ExportedFunctions exportedFunctions() { return {{"get", static_luaGet}}; }

private:
  /**
   * Get public key value.
   * @return public key value or nil if key is empty.
   */
  DECLARE_LUA_FUNCTION(PublicKeyWrapper, luaGet);

  const std::string public_key_;
};

class Timestamp {
public:
  enum Resolution { Millisecond, Microsecond, Undefined };
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
