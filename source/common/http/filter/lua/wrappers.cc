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
    parent_.iterating_ = false;
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
  if (iterating_) {
    luaL_error(state, "cannot create a second iterator before completing the first");
  }

  // The way iteration works is we create an iteration wrapper that snaps pointers to all of
  // the headers. We don't allow modification while an iterator is active. This means that
  // currently if a script breaks out of iteration, further modifications will not be possible
  // because we don't know if they may resume iteration in the future and it isn't safe. There
  // are potentially better ways of handling this but due to GC of the iterator it's very
  // difficult to control safety without tracking every allocated iterator and invalidating them
  // if the map is modified.
  // TODO(mattklein123): Handle explicit iterator invalidation. We should allowing calling code to
  // to destroy the iterator if it should not be used again.
  iterating_ = true;
  HeaderMapIterator::create(state, *this);
  lua_pushcclosure(state, HeaderMapIterator::static_luaPairsIterator, 1);
  return 1;
}

int HeaderMapWrapper::luaRemove(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  headers_.remove(LowerCaseString(key));
  return 0;
}

void HeaderMapWrapper::checkModifiable(lua_State* state) {
  if (iterating_) {
    luaL_error(state, "header map cannot be modified while iterating");
  }

  if (!cb_()) {
    luaL_error(state, "header map can no longer be modified");
  }
}

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
