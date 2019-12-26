#include "extensions/filters/common/lua/wrappers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

int BufferWrapper::luaLength(lua_State* state) {
  lua_pushnumber(state, data_.length());
  return 1;
}

int BufferWrapper::luaGetBytes(lua_State* state) {
  const int index = luaL_checkint(state, 2);
  const int length = luaL_checkint(state, 3);
  if (index < 0 || length < 0 ||
      static_cast<uint64_t>(index) + static_cast<uint64_t>(length) > data_.length()) {
    luaL_error(state, "index/length must be >= 0 and (index + length) must be <= buffer size");
  }

  // TODO(mattklein123): Reduce copies here by using Lua direct buffer builds.
  std::unique_ptr<char[]> data(new char[length]);
  data_.copyOut(index, length, data.get());
  lua_pushlstring(state, data.get(), length);
  return 1;
}

void MetadataMapHelper::setValue(lua_State* state, const ProtobufWkt::Value& value) {
  ProtobufWkt::Value::KindCase kind = value.kind_case();

  switch (kind) {
  case ProtobufWkt::Value::kNullValue:
    return lua_pushnil(state);

  case ProtobufWkt::Value::kNumberValue:
    return lua_pushnumber(state, value.number_value());

  case ProtobufWkt::Value::kBoolValue:
    return lua_pushboolean(state, value.bool_value());

  case ProtobufWkt::Value::kStringValue: {
    const auto& string_value = value.string_value();
    return lua_pushstring(state, string_value.c_str());
  }

  case ProtobufWkt::Value::kStructValue: {
    return createTable(state, value.struct_value().fields());
  }

  case ProtobufWkt::Value::kListValue: {
    const auto& list = value.list_value();
    const int values_size = list.values_size();

    lua_createtable(state, values_size, 0);
    for (int i = 0; i < values_size; i++) {
      // Here we want to build an array (or a list). Array in lua is just a name for table used in a
      // specific way. Basically we want to have: 'elements' table. Where elements[i] is an entry
      // in that table, where key = i and value = list.values[i].
      //
      // Firstly, we need to push the value to the stack.
      setValue(state, list.values(i));

      // Secondly, after the list.value(i) is pushed to the stack, we need to set the 'current
      // element' with that value. The lua_rawseti(state, t, i) helps us to set the value of table t
      // with key i. Given the index of the current element/table in the stack is below the pushed
      // value i.e. -2 and the key (refers to where the element is in the table) is i + 1 (note that
      // in lua index starts from 1), hence we have:
      lua_rawseti(state, -2, i + 1);
    }
    return;
  }

  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void MetadataMapHelper::createTable(lua_State* state,
                                    const Protobuf::Map<std::string, ProtobufWkt::Value>& fields) {
  lua_createtable(state, 0, fields.size());
  for (const auto& field : fields) {
    int top = lua_gettop(state);
    lua_pushstring(state, field.first.c_str());
    setValue(state, field.second);
    lua_settable(state, top);
  }
}

/**
 * Converts the value on top of the Lua stack into a ProtobufWkt::Value.
 * Any Lua types that cannot be directly mapped to Value types will
 * yield an error.
 */
ProtobufWkt::Value MetadataMapHelper::loadValue(lua_State* state) {
  ProtobufWkt::Value value;
  int type = lua_type(state, -1);

  switch (type) {
  case LUA_TNIL:
    value.set_null_value(ProtobufWkt::NullValue());
    break;
  case LUA_TNUMBER:
    value.set_number_value(static_cast<double>(lua_tonumber(state, -1)));
    break;
  case LUA_TBOOLEAN:
    value.set_bool_value(lua_toboolean(state, -1) != 0);
    break;
  case LUA_TTABLE: {
    int length = MetadataMapHelper::tableLength(state);
    if (length > 0) {
      *value.mutable_list_value() = MetadataMapHelper::loadList(state, length);
    } else {
      *value.mutable_struct_value() = MetadataMapHelper::loadStruct(state);
    }
    break;
  }
  case LUA_TSTRING:
    value.set_string_value(lua_tostring(state, -1));
    break;
  default:
    luaL_error(state, "unexpected type '%s' in dynamicMetadata", lua_typename(state, type));
  }

  return value;
}

/**
 * Returns the length of a Lua table if it's actually shaped like a List,
 * i.e. if all the keys are consecutive number values. Otherwise, returns -1.
 */
int MetadataMapHelper::tableLength(lua_State* state) {
  double max = 0;

  lua_pushnil(state);
  while (lua_next(state, -2) != 0) {
    if (lua_type(state, -2) == LUA_TNUMBER) {
      double k = lua_tonumber(state, -2);
      if (floor(k) == k && k >= 1) {
        if (k > max) {
          max = k;
        }
        lua_pop(state, 1);
        continue;
      }
    }
    lua_pop(state, 2);
    return -1;
  }
  return static_cast<int>(max);
}

ProtobufWkt::ListValue MetadataMapHelper::loadList(lua_State* state, int length) {
  ProtobufWkt::ListValue list;

  for (int i = 1; i <= length; i++) {
    lua_rawgeti(state, -1, i);
    *list.add_values() = MetadataMapHelper::loadValue(state);
    lua_pop(state, 1);
  }

  return list;
}

ProtobufWkt::Struct MetadataMapHelper::loadStruct(lua_State* state) {
  ProtobufWkt::Struct struct_obj;

  lua_pushnil(state);
  while (lua_next(state, -2) != 0) {
    int key_type = lua_type(state, -2);
    if (key_type != LUA_TSTRING) {
      luaL_error(state, "unexpected type %s in table key (only string keys are supported)",
                 lua_typename(state, key_type));
    }
    const char* key = lua_tostring(state, -2);
    (*struct_obj.mutable_fields())[key] = MetadataMapHelper::loadValue(state);
    lua_pop(state, 1);
  }

  return struct_obj;
}

MetadataMapIterator::MetadataMapIterator(MetadataMapWrapper& parent)
    : parent_{parent}, current_{parent.metadata_.fields().begin()} {}

int MetadataMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == parent_.metadata_.fields().end()) {
    parent_.iterator_.reset();
    return 0;
  }

  lua_pushstring(state, current_->first.c_str());
  MetadataMapHelper::setValue(state, current_->second);

  current_++;
  return 2;
}

int MetadataMapWrapper::luaGet(lua_State* state) {
  const char* key = luaL_checkstring(state, 2);
  const auto filter_it = metadata_.fields().find(key);
  if (filter_it == metadata_.fields().end()) {
    return 0;
  }

  MetadataMapHelper::setValue(state, filter_it->second);
  return 1;
}

int MetadataMapWrapper::luaPairs(lua_State* state) {
  if (iterator_.get() != nullptr) {
    luaL_error(state, "cannot create a second iterator before completing the first");
  }

  iterator_.reset(MetadataMapIterator::create(state, *this), true);
  lua_pushcclosure(state, MetadataMapIterator::static_luaPairsIterator, 1);
  return 1;
}

int ConnectionWrapper::luaSsl(lua_State* state) {
  const auto& ssl = connection_->ssl();
  if (ssl != nullptr) {
    if (ssl_connection_wrapper_.get() != nullptr) {
      ssl_connection_wrapper_.pushStack();
    } else {
      ssl_connection_wrapper_.reset(SslConnectionWrapper::create(state, ssl), true);
    }
  } else {
    lua_pushnil(state);
  }
  return 1;
}

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
