#include "source/extensions/filters/http/lua/wrappers.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/common/lua/wrappers.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

HeaderMapIterator::HeaderMapIterator(HeaderMapWrapper& parent) : parent_(parent) {
  entries_.reserve(parent_.headers_.size());
  parent_.headers_.iterate(
      [this](const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
        entries_.push_back(&header);
        return Envoy::Http::HeaderMap::Iterate::Continue;
      });
}

int HeaderMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == entries_.size()) {
    parent_.iterator_.reset();
    return 0;
  } else {
    const absl::string_view key_view(entries_[current_]->key().getStringView());
    lua_pushlstring(state, key_view.data(), key_view.size());
    const absl::string_view value_view(entries_[current_]->value().getStringView());
    lua_pushlstring(state, value_view.data(), value_view.size());
    current_++;
    return 2;
  }
}

int HeaderMapWrapper::luaAdd(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  const char* value = luaL_checkstring(state, 3);
  headers_.addCopy(Envoy::Http::LowerCaseString(key), value);
  return 0;
}

int HeaderMapWrapper::luaGet(lua_State* state) {
  absl::string_view key = Filters::Common::Lua::getStringViewFromLuaString(state, 2);
  const Envoy::Http::HeaderUtility::GetAllOfHeaderAsStringResult value =
      Envoy::Http::HeaderUtility::getAllOfHeaderAsString(headers_,
                                                         Envoy::Http::LowerCaseString(key));
  if (value.result().has_value()) {
    lua_pushlstring(state, value.result().value().data(), value.result().value().size());
    return 1;
  } else {
    return 0;
  }
}

int HeaderMapWrapper::luaGetAtIndex(lua_State* state) {
  absl::string_view key = Filters::Common::Lua::getStringViewFromLuaString(state, 2);
  const int index = luaL_checknumber(state, 3);
  const Envoy::Http::HeaderMap::GetResult header_value =
      headers_.get(Envoy::Http::LowerCaseString(key));
  if (index >= 0 && header_value.size() > static_cast<uint64_t>(index)) {
    absl::string_view value = header_value[index]->value().getStringView();
    lua_pushlstring(state, value.data(), value.size());
    return 1;
  }
  return 0;
}

int HeaderMapWrapper::luaGetNumValues(lua_State* state) {
  absl::string_view key = Filters::Common::Lua::getStringViewFromLuaString(state, 2);
  const Envoy::Http::HeaderMap::GetResult header_value =
      headers_.get(Envoy::Http::LowerCaseString(key));
  lua_pushnumber(state, header_value.size());
  return 1;
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
  const Envoy::Http::LowerCaseString lower_key(key);

  headers_.setCopy(lower_key, value);

  return 0;
}

int HeaderMapWrapper::luaRemove(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  headers_.remove(Envoy::Http::LowerCaseString(key));
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

int HeaderMapWrapper::luaSetHttp1ReasonPhrase(lua_State* state) {
  checkModifiable(state);

  size_t input_size = 0;
  const char* phrase = luaL_checklstring(state, 2, &input_size);

  Envoy::Http::StatefulHeaderKeyFormatterOptRef formatter(headers_.formatter());

  if (!formatter.has_value()) {
    using envoy::extensions::http::header_formatters::preserve_case::v3::
        PreserveCaseFormatterConfig;
    using Envoy::Http::ResponseHeaderMapImpl;
    using Envoy::Http::StatefulHeaderKeyFormatter;
    using Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter;

    // Casting here to make sure the call is in the right (response) context
    ResponseHeaderMapImpl* map = dynamic_cast<ResponseHeaderMapImpl*>(&headers_);
    if (map) {
      std::unique_ptr<StatefulHeaderKeyFormatter> fmt =
          std::make_unique<PreserveCaseHeaderFormatter>(true, PreserveCaseFormatterConfig::DEFAULT);
      fmt->setReasonPhrase(absl::string_view(phrase, input_size));
      map->setFormatter(std::move(fmt));
    }
  } else {
    formatter->setReasonPhrase(absl::string_view(phrase, input_size));
  }

  return 0;
}

int PublicKeyWrapper::luaGet(lua_State* state) {
  if (public_key_.empty()) {
    lua_pushnil(state);
  } else {
    lua_pushlstring(state, public_key_.data(), public_key_.size());
  }
  return 1;
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
