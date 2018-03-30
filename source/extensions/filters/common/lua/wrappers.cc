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

void MetadataMapWrapper::setValue(lua_State* state, const ProtobufWkt::Value& value) {
  ProtobufWkt::Value::KindCase kind = value.kind_case();

  switch (kind) {
  case ProtobufWkt::Value::kNullValue:
    return lua_pushnil(state);

  case ProtobufWkt::Value::kNumberValue:
    return lua_pushnumber(state, value.number_value());

  case ProtobufWkt::Value::kBoolValue:
    return lua_pushboolean(state, value.bool_value());

  case ProtobufWkt::Value::kStringValue: {
    const auto string_value = value.string_value();
    return lua_pushstring(state, string_value.c_str());
  }

  case ProtobufWkt::Value::kStructValue: {
    return createTable(state, value.struct_value().fields());
  }

  case ProtobufWkt::Value::kListValue: {
    const auto list = value.list_value();
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
    NOT_REACHED;
  }
}

void MetadataMapWrapper::createTable(
    lua_State* state, const ProtobufWkt::Map<std::string, ProtobufWkt::Value>& fields) {
  lua_createtable(state, 0, fields.size());
  for (const auto& field : fields) {
    int top = lua_gettop(state);
    lua_pushstring(state, field.first.c_str());
    setValue(state, field.second);
    lua_settable(state, top);
  }
}

MetadataMapIterator::MetadataMapIterator(MetadataMapWrapper& parent)
    : parent_{parent}, current_{parent.metadata_.fields().begin()} {}

int MetadataMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == parent_.metadata_.fields().end()) {
    parent_.iterator_.reset();
    return 0;
  }

  lua_pushstring(state, current_->first.c_str());
  parent_.setValue(state, current_->second);

  current_++;
  return 2;
}

int MetadataMapWrapper::luaGet(lua_State* state) {
  const char* key = luaL_checkstring(state, 2);
  const auto filter_it = metadata_.fields().find(key);
  if (filter_it == metadata_.fields().end()) {
    return 0;
  }

  setValue(state, filter_it->second);
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

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
