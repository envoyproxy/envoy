#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/request_info/request_info.h"

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

class DynamicMetadataMapWrapper;

/**
 * Iterator over a dynamic metadata map.
 */
class DynamicMetadataMapIterator : public BaseLuaObject<DynamicMetadataMapIterator> {
public:
  DynamicMetadataMapIterator(DynamicMetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(DynamicMetadataMapIterator, luaPairsIterator);

private:
  DynamicMetadataMapWrapper& parent_;
  Protobuf::Map<Envoy::ProtobufTypes::String, ProtobufWkt::Struct>::const_iterator current_;
};

class DynamicMetadataMapWrapper : public BaseLuaObject<DynamicMetadataMapWrapper> {
public:
  DynamicMetadataMapWrapper(RequestInfo::RequestInfo& request_info) : request_info_{request_info} {}

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

  RequestInfo::RequestInfo& request_info_;
  LuaDeathRef<DynamicMetadataMapIterator> iterator_;

  friend class DynamicMetadataMapIterator;
};

class RequestInfoWrapper : public BaseLuaObject<RequestInfoWrapper> {
public:
  RequestInfoWrapper(RequestInfo::RequestInfo& request_info) : request_info_{request_info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"dynamicMetadata", static_luaDynamicMetadata}, {"protocol", static_luaProtocol}};
  }

private:
  /**
   * Get a dynamic metadata value from the map.
   * @return value if found or nil.
   */
  DECLARE_LUA_FUNCTION(RequestInfoWrapper, luaDynamicMetadata);

  /**
   * Get current protocol being used.
   * @return string, Http::Protocol.
   */
  DECLARE_LUA_FUNCTION(RequestInfoWrapper, luaProtocol);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // TODO(dio): Check if it is required to always reset in here.
    metadata_wrapper_.reset();
  }

  LuaDeathRef<DynamicMetadataMapWrapper> metadata_wrapper_;
  RequestInfo::RequestInfo& request_info_;
};

typedef std::shared_ptr<const Network::Connection> NetworkConnectionSharedPtr;

class ConnectionWrapper : public BaseLuaObject<ConnectionWrapper> {
public:
  ConnectionWrapper(const Network::Connection* connection) : connection_{connection} {}
  static ExportedFunctions exportedFunctions() { return {{"secure", static_luaSecure}}; }

private:
  /**
   * Check if the connection is secured or not.
   * @return boolean true if secure and false if not.
   */
  DECLARE_LUA_FUNCTION(ConnectionWrapper, luaSecure);

  const NetworkConnectionSharedPtr connection_;
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
