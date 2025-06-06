#include "source/extensions/filters/common/lua/protobuf_converter.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

namespace {

// Helper function to push string to Lua stack
inline void pushLuaString(lua_State* state, const std::string& str) {
  lua_pushlstring(state, str.data(), str.size());
}

// Helper function to push numeric value based on protobuf field type
void pushLuaNumericValue(lua_State* state, const Protobuf::ReflectableMessage& message,
                         const Protobuf::FieldDescriptor* field,
                         const Protobuf::Reflection* reflection) {
  switch (field->cpp_type()) {
  case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    lua_pushnumber(state, reflection->GetInt32(*message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_INT64:
    lua_pushnumber(state, reflection->GetInt64(*message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
    lua_pushnumber(state, reflection->GetUInt32(*message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
    lua_pushnumber(state, reflection->GetUInt64(*message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    lua_pushnumber(state, reflection->GetDouble(*message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    lua_pushnumber(state, reflection->GetFloat(*message, field));
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
}

} // namespace

void ProtobufConverterUtils::pushLuaTableFromMessage(lua_State* state,
                                                     const Protobuf::Message& message) {
  auto reflectable = Envoy::createReflectableMessage(message);
  pushLuaTableFromMessage(state, reflectable);
}

void ProtobufConverterUtils::pushLuaTableFromMessage(
    lua_State* state, const Protobuf::ReflectableMessage& reflectable) {
  const auto* descriptor = reflectable->GetDescriptor();
  const auto* reflection = reflectable->GetReflection();

  // Create a table for the message
  lua_createtable(state, 0, descriptor->field_count());

  // Process all fields
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);

    // Skip fields that aren't set
    if (field->is_repeated()) {
      if (reflection->FieldSize(*reflectable, field) == 0) {
        continue;
      }
    } else if (!reflection->HasField(*reflectable, field)) {
      continue;
    }

    // Add the field to the table
    pushLuaString(state, field->name());
    pushLuaValueFromField(state, reflectable, field);
    lua_rawset(state, -3);
  }
}

void ProtobufConverterUtils::pushLuaValueFromField(lua_State* state,
                                                   const Protobuf::ReflectableMessage& message,
                                                   const Protobuf::FieldDescriptor* field) {
  const Protobuf::Reflection* reflection = message->GetReflection();

  // Check for map fields first, before checking for repeated fields
  if (field->is_map()) {
    pushLuaTableFromMapField(state, message, field);
    return;
  }

  if (field->is_repeated()) {
    pushLuaArrayFromRepeatedField(state, message, field);
    return;
  }

  switch (field->cpp_type()) {
  case Protobuf::FieldDescriptor::CPPTYPE_INT32:
  case Protobuf::FieldDescriptor::CPPTYPE_INT64:
  case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
  case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
  case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
  case Protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    pushLuaNumericValue(state, message, field, reflection);
    break;

  case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
    lua_pushboolean(state, reflection->GetBool(*message, field));
    break;

  case Protobuf::FieldDescriptor::CPPTYPE_STRING: {
    const std::string& value = reflection->GetString(*message, field);
    pushLuaString(state, value);
    break;
  }

  case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
    const auto& msg = reflection->GetMessage(*message, field);
    pushLuaTableFromMessage(state, msg);
    break;
  }

  case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
    lua_pushnumber(state, reflection->GetEnumValue(*message, field));
    break;

  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
}

void ProtobufConverterUtils::pushLuaTableFromMapField(lua_State* state,
                                                      const Protobuf::ReflectableMessage& message,
                                                      const Protobuf::FieldDescriptor* field) {
  const Protobuf::Reflection* reflection = message->GetReflection();
  const int size = reflection->FieldSize(*message, field);

  lua_createtable(state, 0, size);

  for (int i = 0; i < size; ++i) {
    // Get the map entry message
    const auto& entry_message = reflection->GetRepeatedMessage(*message, field, i);
    auto entry = Envoy::createReflectableMessage(entry_message);
    const auto* entry_descriptor = entry->GetDescriptor();
    const auto* entry_reflection = entry->GetReflection();

    // Get key and value field descriptors using the standard protobuf map field names
    const auto* key_field =
        entry_descriptor->FindFieldByName(std::string(ProtobufMap::KEY_FIELD_NAME));
    const auto* value_field =
        entry_descriptor->FindFieldByName(std::string(ProtobufMap::VALUE_FIELD_NAME));

    if (key_field == nullptr || value_field == nullptr) {
      continue; // Skip if this isn't a proper map entry
    }

    // Push the key
    switch (key_field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING: {
      const std::string& key = entry_reflection->GetString(*entry, key_field);
      pushLuaString(state, key);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
      lua_pushnumber(state, entry_reflection->GetInt32(*entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state, entry_reflection->GetInt64(*entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
      lua_pushnumber(state, entry_reflection->GetUInt32(*entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
      lua_pushnumber(state, entry_reflection->GetUInt64(*entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
      lua_pushboolean(state, entry_reflection->GetBool(*entry, key_field));
      break;
    default:
      lua_pushnumber(state, i + 1); // Fallback to 1-based index if key type isn't supported
      break;
    }

    // Push the value
    pushLuaValueFromField(state, entry, value_field);
    lua_rawset(state, -3);
  }
}

void ProtobufConverterUtils::pushLuaArrayFromRepeatedField(
    lua_State* state, const Protobuf::ReflectableMessage& message,
    const Protobuf::FieldDescriptor* field) {
  const Protobuf::Reflection* reflection = message->GetReflection();
  const int size = reflection->FieldSize(*message, field);

  lua_createtable(state, size, 0);

  for (int i = 0; i < size; ++i) {
    // Lua arrays are 1-based
    lua_pushnumber(state, i + 1);

    switch (field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
      lua_pushnumber(state, reflection->GetRepeatedInt32(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state, reflection->GetRepeatedInt64(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
      lua_pushnumber(state, reflection->GetRepeatedUInt32(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
      lua_pushnumber(state, reflection->GetRepeatedUInt64(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      lua_pushnumber(state, reflection->GetRepeatedDouble(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      lua_pushnumber(state, reflection->GetRepeatedFloat(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
      lua_pushboolean(state, reflection->GetRepeatedBool(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_STRING: {
      const std::string& value = reflection->GetRepeatedString(*message, field, i);
      pushLuaString(state, value);
      break;
    }
    case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      pushLuaTableFromMessage(state, reflection->GetRepeatedMessage(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
      lua_pushnumber(state, reflection->GetRepeatedEnumValue(*message, field, i));
      break;
    default:
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
    lua_rawset(state, -3);
  }
}

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
