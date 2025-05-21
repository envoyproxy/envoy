#include "source/extensions/filters/common/lua/protobuf_converter.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

namespace {

// Helper function to push string to Lua stack
void pushLuaString(lua_State* state, const std::string& str) {
  lua_pushlstring(state, str.data(), str.size());
}

} // namespace

void ProtobufConverterUtils::pushLuaTableFromMessage(lua_State* state,
                                                     const Protobuf::Message& message) {
  lua_newtable(state);

  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
  const auto* reflection = reflectable_message->GetReflection();

  std::vector<const Protobuf::FieldDescriptor*> fields;
  reflection->ListFields(*reflectable_message, &fields);

  for (const auto* field : fields) {
    if (field->is_map()) {
      pushLuaTableFromMapField(state, reflectable_message, field);
    } else if (field->is_repeated()) {
      pushLuaArrayFromRepeatedField(state, reflectable_message, field);
    } else {
      pushLuaValueFromField(state, message, field);
    }
    lua_setfield(state, -2, field->name().c_str());
  }
}

void ProtobufConverterUtils::pushLuaValueFromField(lua_State* state,
                                                   const Protobuf::Message& message,
                                                   const Protobuf::FieldDescriptor* field) {
  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
  const auto* reflection = reflectable_message->GetReflection();

  switch (field->cpp_type()) {
  case Protobuf::FieldDescriptor::CPPTYPE_INT32:
  case Protobuf::FieldDescriptor::CPPTYPE_INT64:
    lua_pushnumber(state, reflection->GetInt64(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
  case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
    lua_pushnumber(state, reflection->GetUInt64(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    lua_pushnumber(state, reflection->GetDouble(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    lua_pushnumber(state, reflection->GetFloat(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
    lua_pushboolean(state, reflection->GetBool(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_STRING:
    pushLuaString(state, reflection->GetString(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    pushLuaTableFromMessage(state, reflection->GetMessage(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
    lua_pushnumber(state, reflection->GetEnumValue(*reflectable_message, field));
    break;
  default:
    lua_pushnil(state);
    break;
  }
}

void ProtobufConverterUtils::pushLuaTableFromMapField(lua_State* state,
                                                      const Protobuf::ReflectableMessage& message,
                                                      const Protobuf::FieldDescriptor* field) {
  lua_newtable(state);

  if (!field->is_repeated()) {
    return;
  }

  const auto* reflection = message->GetReflection();
  const int count = reflection->FieldSize(*message, field);

  for (int i = 0; i < count; i++) {
    const auto& entry_msg = reflection->GetRepeatedMessage(*message, field, i);
    Protobuf::ReflectableMessage map_entry = createReflectableMessage(entry_msg);

    const auto* map_desc = entry_msg.GetDescriptor();
    const auto* key_field = map_desc->FindFieldByName("key");
    const auto* value_field = map_desc->FindFieldByName("value");

    if (!key_field || !value_field) {
      continue;
    }

    const auto* entry_reflection = map_entry->GetReflection();

    // Push map key
    switch (key_field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING:
      pushLuaString(state, entry_reflection->GetString(*map_entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state, entry_reflection->GetInt64(*map_entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
    case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
      lua_pushnumber(state, entry_reflection->GetUInt64(*map_entry, key_field));
      break;
    default:
      lua_pushnil(state);
      break;
    }

    // Push map value
    switch (value_field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING:
      pushLuaString(state, entry_reflection->GetString(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      pushLuaTableFromMessage(state, entry_reflection->GetMessage(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state, entry_reflection->GetInt64(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
    case Protobuf::FieldDescriptor::CPPTYPE_UINT64:
      lua_pushnumber(state, entry_reflection->GetUInt64(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      lua_pushnumber(state, entry_reflection->GetDouble(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      lua_pushnumber(state, entry_reflection->GetFloat(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
      lua_pushboolean(state, entry_reflection->GetBool(*map_entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
      lua_pushnumber(state, entry_reflection->GetEnumValue(*map_entry, value_field));
      break;
    default:
      lua_pushnil(state);
      break;
    }

    lua_settable(state, -3);
  }
}

void ProtobufConverterUtils::pushLuaArrayFromRepeatedField(
    lua_State* state, const Protobuf::ReflectableMessage& message,
    const Protobuf::FieldDescriptor* field) {
  lua_newtable(state);
  const auto* reflection = message->GetReflection();
  const int count = reflection->FieldSize(*message, field);

  for (int i = 0; i < count; i++) {
    lua_pushinteger(state, i + 1); // Lua uses 1-based indexing

    switch (field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING:
      pushLuaString(state, reflection->GetRepeatedString(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      pushLuaTableFromMessage(state, reflection->GetRepeatedMessage(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state, reflection->GetRepeatedInt64(*message, field, i));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_UINT32:
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
    case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
      lua_pushnumber(state, reflection->GetRepeatedEnumValue(*message, field, i));
      break;
    default:
      lua_pushnil(state);
      break;
    }

    lua_settable(state, -3);
  }
}

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
