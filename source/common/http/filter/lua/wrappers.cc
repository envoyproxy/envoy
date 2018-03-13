#include "common/http/filter/lua/wrappers.h"

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

HeaderMapIterator::HeaderMapIterator(HeaderMapWrapper& parent) : parent_(parent) {
  entries_.reserve(parent_.headers_.size());
  parent_.headers_.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        HeaderMapIterator* iterator = static_cast<HeaderMapIterator*>(context);
        iterator->entries_.push_back(&header);
        return HeaderMap::Iterate::Continue;
      },
      this);
}

int HeaderMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == entries_.size()) {
    parent_.iterator_.reset();
    return 0;
  } else {
    lua_pushstring(state, entries_[current_]->key().c_str());
    lua_pushstring(state, entries_[current_]->value().c_str());
    current_++;
    return 2;
  }
}

int HeaderMapWrapper::luaAdd(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  const char* value = luaL_checkstring(state, 3);
  headers_.addCopy(LowerCaseString(key), value);
  return 0;
}

int HeaderMapWrapper::luaGet(lua_State* state) {
  const char* key = luaL_checkstring(state, 2);
  const HeaderEntry* entry = headers_.get(LowerCaseString(key));
  if (entry != nullptr) {
    lua_pushstring(state, entry->value().c_str());
    return 1;
  } else {
    return 0;
  }
}

int HeaderMapWrapper::luaPairs(lua_State* state) {
  if (iterator_.get() != nullptr) {
    luaL_error(state, "cannot create a second iterator before completing the first");
  }

  // The way iteration works is we create an iteration wrapper that snaps pointers to all of
  // the headers. We don't allow modification while an iterator is active. This means that
  // currently if a script breaks out of iteration, further modifications will not be possible
  // because we don't know if they may resume iteration in the future and it isn't safe. There
  // are potentially better ways of handling this but due to GC of the iterator it's very
  // difficult to control safety without tracking every allocated iterator and invalidating them
  // if the map is modified.
  iterator_.reset(HeaderMapIterator::create(state, *this), true);
  lua_pushcclosure(state, HeaderMapIterator::static_luaPairsIterator, 1);
  return 1;
}

int HeaderMapWrapper::luaReplace(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  const char* value = luaL_checkstring(state, 3);
  const LowerCaseString lower_key(key);

  HeaderEntry* entry = headers_.get(lower_key);
  if (entry != nullptr) {
    entry->value(value, strlen(value));
  } else {
    headers_.addCopy(lower_key, value);
  }

  return 0;
}

int HeaderMapWrapper::luaRemove(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  headers_.remove(LowerCaseString(key));
  return 0;
}

void HeaderMapWrapper::checkModifiable(lua_State* state) {
  if (iterator_.get() != nullptr) {
    luaL_error(state, "header map cannot be modified while iterating");
  }

  if (!cb_()) {
    luaL_error(state, "header map can no longer be modified");
  }
}

void MetadataMapWrapper::setValue(lua_State* state, const ProtobufWkt::Value&& value) {
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
    return createTable(state, std::move(value.struct_value().fields()));
  }

  case ProtobufWkt::Value::kListValue: {
    const auto list = value.list_value();
    const int values_size = list.values_size();

    lua_createtable(state, values_size, 0);
    for (int i = 0; i < values_size; i++) {
      setValue(state, std::move(list.values(i)));
      lua_rawseti(state, -2, i + 1);
    }
    return;
  }

  default:
    NOT_REACHED;
  }
}

void MetadataMapWrapper::createTable(
    lua_State* state, const ProtobufWkt::Map<std::string, ProtobufWkt::Value>&& fields) {
  lua_createtable(state, 0, fields.size());
  for (const auto field : fields) {
    int top = lua_gettop(state);
    lua_pushstring(state, field.first.c_str());
    setValue(state, std::move(field.second));
    lua_settable(state, top);
  }
}

MetadataMapIterator::MetadataMapIterator(MetadataMapWrapper& parent)
    : parent_{parent}, current_{parent.metadata_.filter_metadata().begin()} {}

int MetadataMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == parent_.metadata_.filter_metadata().end()) {
    parent_.iterator_.reset();
    return 0;
  }

  lua_pushstring(state, current_->first.c_str());
  parent_.createTable(state, std::move(current_->second.fields()));

  current_++;
  return 2;
}

int MetadataMapWrapper::luaGet(lua_State* state) {
  const char* filter = luaL_checkstring(state, 2);
  const auto filter_it = metadata_.filter_metadata().find(filter);
  if (filter_it == metadata_.filter_metadata().end()) {
    return 0;
  }

  createTable(state, std::move(filter_it->second.fields()));
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
} // namespace Filter
} // namespace Http
} // namespace Envoy
