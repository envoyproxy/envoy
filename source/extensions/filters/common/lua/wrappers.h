#pragma once

#include "envoy/buffer/buffer.h"

#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/lua/lua.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

/**
 * A wrapper for a constant buffer which cannot be modified by Lua.
 */
class BufferWrapper : public BaseLuaObject<BufferWrapper> {
public:
  BufferWrapper(const Buffer::Instance& data) : data_(data) {}

  static ExportedFunctions exportedFunctions() {
    return {{"length", static_luaLength}, {"getBytes", static_luaGetBytes}};
  }

private:
  /**
   * @return int the size in bytes of the buffer.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaLength);

  /**
   * Get bytes out of a buffer for inspection in Lua.
   * @param 1 (int) starting index of bytes to extract.
   * @param 2 (int) length of bytes to extract.
   * @return string the extracted bytes. Throws an error if the index/length are out of range.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaGetBytes);

  const Buffer::Instance& data_;
};

class MetadataMapWrapper;

struct MetadataMapHelper {
  static void setValue(lua_State* state, const ProtobufWkt::Value& value);
  static void
  createTable(lua_State* state,
              const Protobuf::Map<Envoy::ProtobufTypes::String, ProtobufWkt::Value>& fields);
};

/**
 * Iterator over a metadata map.
 */
class MetadataMapIterator : public BaseLuaObject<MetadataMapIterator> {
public:
  MetadataMapIterator(MetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(MetadataMapIterator, luaPairsIterator);

private:
  MetadataMapWrapper& parent_;
  Protobuf::Map<Envoy::ProtobufTypes::String, ProtobufWkt::Value>::const_iterator current_;
};

/**
 * Lua wrapper for a metadata map.
 */
class MetadataMapWrapper : public BaseLuaObject<MetadataMapWrapper> {
public:
  MetadataMapWrapper(const ProtobufWkt::Struct& metadata) : metadata_{metadata} {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaGet}, {"__pairs", static_luaPairs}};
  }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter.
   * @return string value if found or nil.
   */
  DECLARE_LUA_FUNCTION(MetadataMapWrapper, luaGet);

  /**
   * Implementation of the __pairs metamethod so a metadata wrapper can be iterated over using
   * pairs().
   */
  DECLARE_LUA_FUNCTION(MetadataMapWrapper, luaPairs);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  const ProtobufWkt::Struct metadata_;
  LuaDeathRef<MetadataMapIterator> iterator_;

  friend class MetadataMapIterator;
};

/**
 * Lua wrapper for Ssl::Connection.
 */
class SslConnectionWrapper : public BaseLuaObject<SslConnectionWrapper> {
public:
  SslConnectionWrapper(const Ssl::Connection*) {}
  static ExportedFunctions exportedFunctions() { return {}; }

  // TODO(dio): Add more Lua APIs around Ssl::Connection.
};

/**
 * Lua wrapper for Network::Connection.
 */
class ConnectionWrapper : public BaseLuaObject<ConnectionWrapper> {
public:
  ConnectionWrapper(const Network::Connection* connection) : connection_{connection} {}
  static ExportedFunctions exportedFunctions() { return {{"ssl", static_luaSsl}}; }

private:
  /**
   * Get the Ssl::Connection wrapper
   * @return object if secured and nil if not.
   */
  DECLARE_LUA_FUNCTION(ConnectionWrapper, luaSsl);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override { ssl_connection_wrapper_.reset(); }

  const Network::Connection* connection_;
  LuaDeathRef<SslConnectionWrapper> ssl_connection_wrapper_;
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
