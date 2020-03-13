#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "common/crypto/utility.h"

#include "extensions/common/crypto/crypto_impl.h"
#include "extensions/filters/common/lua/lua.h"

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

  Http::HeaderMap& headers_;
  CheckModifiableCb cb_;
  Filters::Common::Lua::LuaDeathRef<HeaderMapIterator> iterator_;

  friend class HeaderMapIterator;
};

class DynamicMetadataMapWrapper;
class StreamInfoWrapper;

/**
 * Iterator over a dynamic metadata map.
 */
class DynamicMetadataMapIterator
    : public Filters::Common::Lua::BaseLuaObject<DynamicMetadataMapIterator> {
public:
  DynamicMetadataMapIterator(DynamicMetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(DynamicMetadataMapIterator, luaPairsIterator);

private:
  DynamicMetadataMapWrapper& parent_;
  Protobuf::Map<std::string, ProtobufWkt::Struct>::const_iterator current_;
};

/**
 * Lua wrapper for a dynamic metadata.
 */
class DynamicMetadataMapWrapper
    : public Filters::Common::Lua::BaseLuaObject<DynamicMetadataMapWrapper> {
public:
  DynamicMetadataMapWrapper(StreamInfoWrapper& parent) : parent_{parent} {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaGet}, {"set", static_luaSet}, {"__pairs", static_luaPairs}};
  }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter name.
   * @return value if found or nil.
   */
  DECLARE_LUA_FUNCTION(DynamicMetadataMapWrapper, luaGet);

  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter name.
   * @param 2 (string or table): key.
   * @param 3 (string or table): value.
   * @return nil.
   */
  DECLARE_LUA_FUNCTION(DynamicMetadataMapWrapper, luaSet);

  /**
   * Implementation of the __pairs metamethod so a dynamic metadata wrapper can be iterated over
   * using pairs().
   */
  DECLARE_LUA_FUNCTION(DynamicMetadataMapWrapper, luaPairs);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  // To get reference to parent's (StreamInfoWrapper) stream info member.
  StreamInfo::StreamInfo& streamInfo();

  StreamInfoWrapper& parent_;
  Filters::Common::Lua::LuaDeathRef<DynamicMetadataMapIterator> iterator_;

  friend class DynamicMetadataMapIterator;
};

/**
 * Lua wrapper for a stream info.
 */
class StreamInfoWrapper : public Filters::Common::Lua::BaseLuaObject<StreamInfoWrapper> {
public:
  StreamInfoWrapper(StreamInfo::StreamInfo& stream_info) : stream_info_{stream_info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"protocol", static_luaProtocol}, {"dynamicMetadata", static_luaDynamicMetadata}};
  }

private:
  /**
   * Get current protocol being used.
   * @return string representation of Http::Protocol.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaProtocol);

  /**
   * Get reference to stream info dynamic metadata object.
   * @return DynamicMetadataMapWrapper representation of StreamInfo dynamic metadata.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDynamicMetadata);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override { dynamic_metadata_wrapper_.reset(); }

  StreamInfo::StreamInfo& stream_info_;
  Filters::Common::Lua::LuaDeathRef<DynamicMetadataMapWrapper> dynamic_metadata_wrapper_;

  friend class DynamicMetadataMapWrapper;
};

/**
 * Lua wrapper for EVP_PKEY.
 */
class PublicKeyWrapper : public Filters::Common::Lua::BaseLuaObject<PublicKeyWrapper> {
public:
  PublicKeyWrapper(Envoy::Common::Crypto::CryptoObjectPtr key) : public_key_(std::move(key)) {}
  static ExportedFunctions exportedFunctions() { return {{"get", static_luaGet}}; }

private:
  /**
   * Get a pointer to public key.
   * @return pointer to public key.
   */
  DECLARE_LUA_FUNCTION(PublicKeyWrapper, luaGet);

  Envoy::Common::Crypto::CryptoObjectPtr public_key_;
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
