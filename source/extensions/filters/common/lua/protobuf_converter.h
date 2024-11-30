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
   * Convert a protobuf message to a Lua table
   * @param state the Lua state
   * @param message the protobuf message to convert
   */
  static void messageToLuaTable(lua_State* state, const Protobuf::Message& message);

private:
  /**
   * Helper function to convert a protobuf field to a Lua value
   * @param state the Lua state
   * @param message the protobuf message containing the field
   * @param field the field descriptor
   */
  static void fieldToLuaValue(lua_State* state, const Protobuf::Message& message,
                              const Protobuf::FieldDescriptor* field);
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
