#include "extensions/filters/http/lua/wrappers.h"

#include "common/http/utility.h"

#include "extensions/filters/common/lua/wrappers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

HeaderMapIterator::HeaderMapIterator(HeaderMapWrapper& parent) : parent_(parent) {
  entries_.reserve(parent_.headers_.size());
  parent_.headers_.iterate([this](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    entries_.push_back(&header);
    return Http::HeaderMap::Iterate::Continue;
  });
}

int HeaderMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == entries_.size()) {
    parent_.iterator_.reset();
    return 0;
  } else {
    const absl::string_view key_view(entries_[current_]->key().getStringView());
    lua_pushlstring(state, key_view.data(), key_view.length());
    const absl::string_view value_view(entries_[current_]->value().getStringView());
    lua_pushlstring(state, value_view.data(), value_view.length());
    current_++;
    return 2;
  }
}

int HeaderMapWrapper::luaAdd(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  const char* value = luaL_checkstring(state, 3);
  headers_.addCopy(Http::LowerCaseString(key), value);
  return 0;
}

int HeaderMapWrapper::luaGet(lua_State* state) {
  const char* key = luaL_checkstring(state, 2);
  const Http::HeaderEntry* entry = headers_.get(Http::LowerCaseString(key));
  if (entry != nullptr) {
    lua_pushlstring(state, entry->value().getStringView().data(),
                    entry->value().getStringView().length());
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
  const Http::LowerCaseString lower_key(key);

  headers_.setCopy(lower_key, value);

  return 0;
}

int HeaderMapWrapper::luaRemove(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  headers_.remove(Http::LowerCaseString(key));
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

int StreamInfoWrapper::luaProtocol(lua_State* state) {
  lua_pushstring(state, Http::Utility::getProtocolString(stream_info_.protocol().value()).c_str());
  return 1;
}

int StreamInfoWrapper::luaDynamicMetadata(lua_State* state) {
  if (dynamic_metadata_wrapper_.get() != nullptr) {
    dynamic_metadata_wrapper_.pushStack();
  } else {
    dynamic_metadata_wrapper_.reset(DynamicMetadataMapWrapper::create(state, *this), true);
  }
  return 1;
}

DynamicMetadataMapIterator::DynamicMetadataMapIterator(DynamicMetadataMapWrapper& parent)
    : parent_{parent}, current_{parent_.streamInfo().dynamicMetadata().filter_metadata().begin()} {}

StreamInfo::StreamInfo& DynamicMetadataMapWrapper::streamInfo() { return parent_.stream_info_; }

int DynamicMetadataMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == parent_.streamInfo().dynamicMetadata().filter_metadata().end()) {
    parent_.iterator_.reset();
    return 0;
  }

  lua_pushstring(state, current_->first.c_str());
  Filters::Common::Lua::MetadataMapHelper::createTable(state, current_->second.fields());

  current_++;
  return 2;
}

int DynamicMetadataMapWrapper::luaGet(lua_State* state) {
  const char* filter_name = luaL_checkstring(state, 2);
  const auto& metadata = streamInfo().dynamicMetadata().filter_metadata();
  const auto filter_it = metadata.find(filter_name);
  if (filter_it == metadata.end()) {
    return 0;
  }

  Filters::Common::Lua::MetadataMapHelper::createTable(state, filter_it->second.fields());
  return 1;
}

int DynamicMetadataMapWrapper::luaSet(lua_State* state) {
  if (iterator_.get() != nullptr) {
    luaL_error(state, "dynamic metadata map cannot be modified while iterating");
  }

  const char* filter_name = luaL_checkstring(state, 2);
  const char* key = luaL_checkstring(state, 3);

  // MetadataMapHelper::loadValue will convert the value on top of the Lua stack,
  // so push a copy of the 3rd arg ("value") to the top.
  lua_pushvalue(state, 4);

  ProtobufWkt::Struct value;
  (*value.mutable_fields())[key] = Filters::Common::Lua::MetadataMapHelper::loadValue(state);
  streamInfo().setDynamicMetadata(filter_name, value);

  // Pop the copy of the metadata value from the stack.
  lua_pop(state, 1);
  return 0;
}

int DynamicMetadataMapWrapper::luaPairs(lua_State* state) {
  if (iterator_.get() != nullptr) {
    luaL_error(state, "cannot create a second iterator before completing the first");
  }

  iterator_.reset(DynamicMetadataMapIterator::create(state, *this), true);
  lua_pushcclosure(state, DynamicMetadataMapIterator::static_luaPairsIterator, 1);
  return 1;
}

int PublicKeyWrapper::luaGet(lua_State* state) {
  if (public_key_ == nullptr || public_key_.get() == nullptr) {
    lua_pushnil(state);
  } else {
    auto wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PublicKeyObject>(*public_key_);
    EVP_PKEY* pkey = wrapper->getEVP_PKEY();
    if (pkey == nullptr) {
      lua_pushnil(state);
    } else {
      lua_pushlightuserdata(state, public_key_.get());
    }
  }
  return 1;
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
