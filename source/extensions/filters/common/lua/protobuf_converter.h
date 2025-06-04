#pragma once

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "lua.hpp"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

/**
 * Standard field names used in protobuf map entries.
 *
 * In the Protobuf specification, maps are represented as repeated messages with special field
 * names. When a map<K,V> field is defined, the protocol buffer compiler generates a special map
 * entry message type with two fields: 'key' of type K and 'value' of type V.
 *
 * References:
 * - https://protobuf.dev/programming-guides/encoding/#maps
 * - https://developers.google.com/protocol-buffers/docs/proto3#maps
 */
namespace ProtobufMap {
// Standard field name for the key in a map entry message
constexpr absl::string_view KEY_FIELD_NAME = "key";

// Standard field name for the value in a map entry message
constexpr absl::string_view VALUE_FIELD_NAME = "value";
} // namespace ProtobufMap

/**
 * Utility class for converting protobuf messages to Lua tables
 */
class ProtobufConverterUtils {
public:
  /**
   * Push a Lua table onto the stack that contains the converted protobuf message
   * @param state the Lua state
   * @param message the protobuf message to convert
   *
   * This function creates a new Lua table containing the fields of the protobuf message
   * and pushes it onto the Lua stack. The caller is responsible for managing the stack.
   */
  static void pushLuaTableFromMessage(lua_State* state, const Protobuf::Message& message);

  /**
   * Push a Lua table onto the stack that contains the converted protobuf message
   * @param state the Lua state
   * @param message the protobuf message to convert
   *
   * This function creates a new Lua table containing the fields of the protobuf message
   * and pushes it onto the Lua stack. The caller is responsible for managing the stack.
   */
  static void pushLuaTableFromMessage(lua_State* state,
                                      const Protobuf::ReflectableMessage& message);

  /**
   * Push a Lua value onto the stack that represents the value of a field
   * @param state the Lua state
   * @param message the protobuf message containing the field
   * @param field the field descriptor
   *
   * This function inspects the field type and extracts the appropriate value,
   * then pushes that value onto the Lua stack.
   */
  static void pushLuaValueFromField(lua_State* state, const Protobuf::ReflectableMessage& message,
                                    const Protobuf::FieldDescriptor* field);

  /**
   * Push a Lua table onto the stack that represents a protobuf map field
   * @param state the Lua state
   * @param message the protobuf message containing the map field
   * @param field the map field descriptor
   *
   * This function converts a protobuf map field into a Lua table and
   * pushes it onto the Lua stack.
   */
  static void pushLuaTableFromMapField(lua_State* state,
                                       const Protobuf::ReflectableMessage& message,
                                       const Protobuf::FieldDescriptor* field);

  /**
   * Push a Lua array onto the stack that represents a repeated field
   * @param state the Lua state
   * @param message the protobuf message containing the repeated field
   * @param field the repeated field descriptor
   *
   * This function converts a protobuf repeated field into a Lua array and
   * pushes it onto the Lua stack.
   */
  static void pushLuaArrayFromRepeatedField(lua_State* state,
                                            const Protobuf::ReflectableMessage& message,
                                            const Protobuf::FieldDescriptor* field);
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
