#pragma once

#include "source/common/protobuf/protobuf.h"

#include "lua.hpp"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

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

private:
  /**
   * Helper function to push a protobuf field value onto the Lua stack
   * @param state the Lua state
   * @param message the protobuf message containing the field
   * @param field the field descriptor
   *
   * Pushes a Lua value onto the stack that represents the field value.
   * The data type pushed depends on the field's type in the protobuf.
   */
  static void pushLuaValueFromField(lua_State* state, const Protobuf::Message& message,
                                    const Protobuf::FieldDescriptor* field);

  /**
   * Helper function to push a Lua representation of a map field
   * @param state the Lua state
   * @param message the protobuf message
   * @param field the map field descriptor
   */
  static void pushLuaTableFromMapField(lua_State* state,
                                       const Protobuf::ReflectableMessage& message,
                                       const Protobuf::FieldDescriptor* field);

  /**
   * Helper function to push a Lua array from a repeated field
   * @param state the Lua state
   * @param message the protobuf message
   * @param field the repeated field descriptor
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
