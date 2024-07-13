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

class DynamicMetadataMapWrapper;
class StreamInfoWrapper;
class ConnectionDynamicMetadataMapWrapper;
class ConnectionStreamInfoWrapper;

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
 * Iterator over a network filter dynamic metadata map.
 */
class ConnectionDynamicMetadataMapIterator
    : public Filters::Common::Lua::BaseLuaObject<ConnectionDynamicMetadataMapIterator> {
public:
  ConnectionDynamicMetadataMapIterator(ConnectionDynamicMetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(ConnectionDynamicMetadataMapIterator,
                      luaConnectionDynamicMetadataPairsIterator);

private:
  ConnectionDynamicMetadataMapWrapper& parent_;
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
 * Lua wrapper for a network filter dynamic metadata.
 */
class ConnectionDynamicMetadataMapWrapper
    : public Filters::Common::Lua::BaseLuaObject<ConnectionDynamicMetadataMapWrapper> {
public:
  ConnectionDynamicMetadataMapWrapper(ConnectionStreamInfoWrapper& parent) : parent_{parent} {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaConnectionDynamicMetadataGet},
            {"__pairs", static_luaConnectionDynamicMetadataPairs}};
  }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter name.
   * @return value if found or nil.
   */
  DECLARE_LUA_FUNCTION(ConnectionDynamicMetadataMapWrapper, luaConnectionDynamicMetadataGet);

  /**
   * Implementation of the __pairs meta method so a dynamic metadata wrapper can be iterated over
   * using pairs().
   */
  DECLARE_LUA_FUNCTION(ConnectionDynamicMetadataMapWrapper, luaConnectionDynamicMetadataPairs);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  // To get reference to parent's (StreamInfoWrapper) stream info member.
  const StreamInfo::StreamInfo& streamInfo();

  ConnectionStreamInfoWrapper& parent_;
  Filters::Common::Lua::LuaDeathRef<ConnectionDynamicMetadataMapIterator> iterator_;

  friend class ConnectionDynamicMetadataMapIterator;
};

/**
 * Lua wrapper for a stream info.
 */
class StreamInfoWrapper : public Filters::Common::Lua::BaseLuaObject<StreamInfoWrapper> {
public:
  StreamInfoWrapper(StreamInfo::StreamInfo& stream_info) : stream_info_{stream_info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"protocol", static_luaProtocol},
            {"dynamicMetadata", static_luaDynamicMetadata},
            {"downstreamLocalAddress", static_luaDownstreamLocalAddress},
            {"downstreamDirectRemoteAddress", static_luaDownstreamDirectRemoteAddress},
            {"downstreamRemoteAddress", static_luaDownstreamRemoteAddress},
            {"downstreamSslConnection", static_luaDownstreamSslConnection},
            {"requestedServerName", static_luaRequestedServerName}};
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

  /**
   * Get reference to stream info downstreamSslConnection.
   * @return SslConnectionWrapper representation of StreamInfo downstream SSL connection.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamSslConnection);

  /**
   * Get current downstream local address
   * @return string representation of downstream local address.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamLocalAddress);

  /**
   * Get current direct downstream remote address
   * @return string representation of downstream directly connected address.
   * This is equivalent to the address of the physical connection.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamDirectRemoteAddress);

  /**
   * Get current downstream remote address
   * @return string representation of downstream remote address.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamRemoteAddress);

  /**
   * Get requested server name
   * @return requested server name (e.g. SNI in TLS), if any.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaRequestedServerName);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    dynamic_metadata_wrapper_.reset();
    downstream_ssl_connection_.reset();
  }

  StreamInfo::StreamInfo& stream_info_;
  Filters::Common::Lua::LuaDeathRef<DynamicMetadataMapWrapper> dynamic_metadata_wrapper_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::SslConnectionWrapper>
      downstream_ssl_connection_;

  friend class DynamicMetadataMapWrapper;
};

/**
 * Lua wrapper for a network connection's stream info.
 */
class ConnectionStreamInfoWrapper
    : public Filters::Common::Lua::BaseLuaObject<ConnectionStreamInfoWrapper> {
public:
  ConnectionStreamInfoWrapper(const StreamInfo::StreamInfo& connection_stream_info)
      : connection_stream_info_{connection_stream_info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"dynamicMetadata", static_luaConnectionDynamicMetadata}};
  }

private:
  /**
   * Get reference to stream info dynamic metadata object.
   * @return ConnectionDynamicMetadataMapWrapper representation of StreamInfo dynamic metadata.
   */
  DECLARE_LUA_FUNCTION(ConnectionStreamInfoWrapper, luaConnectionDynamicMetadata);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override { connection_dynamic_metadata_wrapper_.reset(); }

  const StreamInfo::StreamInfo& connection_stream_info_;
  Filters::Common::Lua::LuaDeathRef<ConnectionDynamicMetadataMapWrapper>
      connection_dynamic_metadata_wrapper_;

  friend class ConnectionDynamicMetadataMapWrapper;
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
