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

// Helper class to handle map field conversion
class MapFieldConverter {
public:
  MapFieldConverter(lua_State* state, const Protobuf::ReflectableMessage& message,
                    const Protobuf::FieldDescriptor* field)
      : state_(state), message_(message), field_(field) {}

  void convert() {
    lua_newtable(state_);
    if (!field_->is_repeated()) {
      return;
    }

    const auto* reflection = message_->GetReflection();
    const int count = reflection->FieldSize(*message_, field_);

    for (int i = 0; i < count; i++) {
      convertMapEntry(reflection->GetRepeatedMessage(*message_, field_, i));
    }
  }

private:
  void convertMapEntry(const Protobuf::Message& entry_msg) {
    Protobuf::ReflectableMessage map_entry = createReflectableMessage(entry_msg);
    const auto* map_desc = entry_msg.GetDescriptor();
    const auto* key_field = map_desc->FindFieldByName("key");
    const auto* value_field = map_desc->FindFieldByName("value");

    if (!key_field || !value_field) {
      return;
    }

    const auto* reflection = map_entry->GetReflection();
    pushMapKey(reflection, map_entry, key_field);
    pushMapValue(reflection, map_entry, value_field);
    lua_settable(state_, -3);
  }

  void pushMapKey(const Protobuf::Reflection* reflection, const Protobuf::ReflectableMessage& entry,
                  const Protobuf::FieldDescriptor* key_field) {
    switch (key_field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING:
      pushLuaString(state_, reflection->GetString(*entry, key_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state_, reflection->GetInt64(*entry, key_field));
      break;
    default:
      lua_pushnil(state_);
      break;
    }
  }

  void pushMapValue(const Protobuf::Reflection* reflection,
                    const Protobuf::ReflectableMessage& entry,
                    const Protobuf::FieldDescriptor* value_field) {
    switch (value_field->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING:
      pushLuaString(state_, reflection->GetString(*entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      ProtobufConverterUtils::messageToLuaTable(state_,
                                                reflection->GetMessage(*entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state_, reflection->GetInt64(*entry, value_field));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
      lua_pushboolean(state_, reflection->GetBool(*entry, value_field));
      break;
    default:
      lua_pushnil(state_);
      break;
    }
  }

  lua_State* state_;
  const Protobuf::ReflectableMessage& message_;
  const Protobuf::FieldDescriptor* field_;
};

// Helper class to handle repeated field conversion
class RepeatedFieldConverter {
public:
  RepeatedFieldConverter(lua_State* state, const Protobuf::ReflectableMessage& message,
                         const Protobuf::FieldDescriptor* field)
      : state_(state), message_(message), field_(field) {}

  void convert() {
    lua_newtable(state_);
    const auto* reflection = message_->GetReflection();
    const int count = reflection->FieldSize(*message_, field_);

    for (int i = 0; i < count; i++) {
      lua_pushinteger(state_, i + 1); // Lua uses 1-based indexing
      pushRepeatedValue(reflection, i);
      lua_settable(state_, -3);
    }
  }

private:
  void pushRepeatedValue(const Protobuf::Reflection* reflection, int index) {
    switch (field_->cpp_type()) {
    case Protobuf::FieldDescriptor::CPPTYPE_STRING:
      pushLuaString(state_, reflection->GetRepeatedString(*message_, field_, index));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      ProtobufConverterUtils::messageToLuaTable(
          state_, reflection->GetRepeatedMessage(*message_, field_, index));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_INT32:
    case Protobuf::FieldDescriptor::CPPTYPE_INT64:
      lua_pushnumber(state_, reflection->GetRepeatedInt64(*message_, field_, index));
      break;
    case Protobuf::FieldDescriptor::CPPTYPE_BOOL:
      lua_pushboolean(state_, reflection->GetRepeatedBool(*message_, field_, index));
      break;
    default:
      lua_pushnil(state_);
      break;
    }
  }

  lua_State* state_;
  const Protobuf::ReflectableMessage& message_;
  const Protobuf::FieldDescriptor* field_;
};

} // namespace

void ProtobufConverterUtils::messageToLuaTable(lua_State* state, const Protobuf::Message& message) {
  lua_newtable(state);

  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
  const auto* reflection = reflectable_message->GetReflection();

  std::vector<const Protobuf::FieldDescriptor*> fields;
  reflection->ListFields(*reflectable_message, &fields);

  for (const auto* field : fields) {
    pushLuaString(state, field->name());

    if (field->is_map()) {
      MapFieldConverter(state, reflectable_message, field).convert();
    } else if (field->is_repeated()) {
      RepeatedFieldConverter(state, reflectable_message, field).convert();
    } else {
      fieldToLuaValue(state, message, field);
    }

    lua_settable(state, -3);
  }
}

void ProtobufConverterUtils::fieldToLuaValue(lua_State* state, const Protobuf::Message& message,
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
    messageToLuaTable(state, reflection->GetMessage(*reflectable_message, field));
    break;
  case Protobuf::FieldDescriptor::CPPTYPE_ENUM:
    lua_pushnumber(state, reflection->GetEnumValue(*reflectable_message, field));
    break;
  default:
    lua_pushnil(state);
    break;
  }
}

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
