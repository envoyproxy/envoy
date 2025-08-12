#include "source/extensions/filters/http/lua/wrappers.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/lua/protobuf_converter.h"
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

int StreamInfoWrapper::luaProtocol(lua_State* state) {
  const std::string& protocol =
      Envoy::Http::Utility::getProtocolString(stream_info_.protocol().value());
  lua_pushlstring(state, protocol.data(), protocol.size());
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

int StreamInfoWrapper::luaDynamicTypedMetadata(lua_State* state) {
  // Get the typed metadata from the stream's metadata
  const auto& typed_metadata = stream_info_.dynamicMetadata().typed_filter_metadata();
  return Filters::Common::Lua::ProtobufConverterUtils::processDynamicTypedMetadataFromLuaCall(
      state, typed_metadata);
}

int StreamInfoWrapper::luaFilterState(lua_State* state) {
  if (filter_state_wrapper_.get() != nullptr) {
    filter_state_wrapper_.pushStack();
  } else {
    filter_state_wrapper_.reset(FilterStateWrapper::create(state, *this), true);
  }
  return 1;
}

int ConnectionStreamInfoWrapper::luaConnectionDynamicMetadata(lua_State* state) {
  if (connection_dynamic_metadata_wrapper_.get() != nullptr) {
    connection_dynamic_metadata_wrapper_.pushStack();
  } else {
    connection_dynamic_metadata_wrapper_.reset(
        ConnectionDynamicMetadataMapWrapper::create(state, *this), true);
  }
  return 1;
}

int StreamInfoWrapper::luaDownstreamSslConnection(lua_State* state) {
  const auto& ssl = stream_info_.downstreamAddressProvider().sslConnection();
  if (ssl != nullptr) {
    if (downstream_ssl_connection_.get() != nullptr) {
      downstream_ssl_connection_.pushStack();
    } else {
      downstream_ssl_connection_.reset(
          Filters::Common::Lua::SslConnectionWrapper::create(state, *ssl), true);
    }
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int ConnectionStreamInfoWrapper::luaConnectionDynamicTypedMetadata(lua_State* state) {
  // Get the typed metadata from the connection's metadata
  const auto& typed_metadata = connection_stream_info_.dynamicMetadata().typed_filter_metadata();
  return Filters::Common::Lua::ProtobufConverterUtils::processDynamicTypedMetadataFromLuaCall(
      state, typed_metadata);
}

int StreamInfoWrapper::luaDownstreamLocalAddress(lua_State* state) {
  const std::string& local_address =
      stream_info_.downstreamAddressProvider().localAddress()->asString();
  lua_pushlstring(state, local_address.data(), local_address.size());
  return 1;
}

int StreamInfoWrapper::luaDownstreamDirectLocalAddress(lua_State* state) {
  const std::string& local_address =
      stream_info_.downstreamAddressProvider().directLocalAddress()->asString();
  lua_pushlstring(state, local_address.data(), local_address.size());
  return 1;
}

int StreamInfoWrapper::luaDownstreamDirectRemoteAddress(lua_State* state) {
  const std::string& direct_remote_address =
      stream_info_.downstreamAddressProvider().directRemoteAddress()->asString();
  lua_pushlstring(state, direct_remote_address.data(), direct_remote_address.size());
  return 1;
}

int StreamInfoWrapper::luaDownstreamRemoteAddress(lua_State* state) {
  const std::string& remote_address =
      stream_info_.downstreamAddressProvider().remoteAddress()->asString();
  lua_pushlstring(state, remote_address.data(), remote_address.size());
  return 1;
}

int StreamInfoWrapper::luaRequestedServerName(lua_State* state) {
  absl::string_view requested_serve_name =
      stream_info_.downstreamAddressProvider().requestedServerName();
  lua_pushlstring(state, requested_serve_name.data(), requested_serve_name.size());
  return 1;
}

int StreamInfoWrapper::luaRouteName(lua_State* state) {
  const std::string& route_name = stream_info_.getRouteName();
  lua_pushlstring(state, route_name.data(), route_name.length());
  return 1;
}

int StreamInfoWrapper::luaVirtualClusterName(lua_State* state) {
  const absl::optional<std::string>& name = stream_info_.virtualClusterName();
  if (name.has_value()) {
    const std::string& virtual_cluster_name = name.value();
    lua_pushlstring(state, virtual_cluster_name.data(), virtual_cluster_name.length());
  } else {
    lua_pushlstring(state, "", 0);
  }
  return 1;
}

DynamicMetadataMapIterator::DynamicMetadataMapIterator(DynamicMetadataMapWrapper& parent)
    : parent_{parent}, current_{parent_.streamInfo().dynamicMetadata().filter_metadata().begin()} {}

StreamInfo::StreamInfo& DynamicMetadataMapWrapper::streamInfo() { return parent_.stream_info_; }

ConnectionDynamicMetadataMapIterator::ConnectionDynamicMetadataMapIterator(
    ConnectionDynamicMetadataMapWrapper& parent)
    : parent_{parent}, current_{parent_.streamInfo().dynamicMetadata().filter_metadata().begin()} {}

const StreamInfo::StreamInfo& ConnectionDynamicMetadataMapWrapper::streamInfo() {
  return parent_.connection_stream_info_;
}

int DynamicMetadataMapIterator::luaPairsIterator(lua_State* state) {
  if (current_ == parent_.streamInfo().dynamicMetadata().filter_metadata().end()) {
    parent_.iterator_.reset();
    return 0;
  }

  lua_pushlstring(state, current_->first.data(), current_->first.size());
  Filters::Common::Lua::MetadataMapHelper::createTable(state, current_->second.fields());

  current_++;
  return 2;
}

int ConnectionDynamicMetadataMapIterator::luaConnectionDynamicMetadataPairsIterator(
    lua_State* state) {
  if (current_ == parent_.streamInfo().dynamicMetadata().filter_metadata().end()) {
    parent_.iterator_.reset();
    return 0;
  }

  lua_pushlstring(state, current_->first.data(), current_->first.size());
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

int ConnectionDynamicMetadataMapWrapper::luaConnectionDynamicMetadataGet(lua_State* state) {
  const char* filter_name = luaL_checkstring(state, 2);
  const auto& metadata = streamInfo().dynamicMetadata().filter_metadata();
  const auto filter_it = metadata.find(filter_name);
  if (filter_it == metadata.end()) {
    return 0;
  }

  Filters::Common::Lua::MetadataMapHelper::createTable(state, filter_it->second.fields());
  return 1;
}

int ConnectionDynamicMetadataMapWrapper::luaConnectionDynamicMetadataPairs(lua_State* state) {
  if (iterator_.get() != nullptr) {
    luaL_error(state, "cannot create a second iterator before completing the first");
  }

  iterator_.reset(ConnectionDynamicMetadataMapIterator::create(state, *this), true);
  lua_pushcclosure(
      state, ConnectionDynamicMetadataMapIterator::static_luaConnectionDynamicMetadataPairsIterator,
      1);
  return 1;
}

int PublicKeyWrapper::luaGet(lua_State* state) {
  if (public_key_.empty()) {
    lua_pushnil(state);
  } else {
    lua_pushlstring(state, public_key_.data(), public_key_.size());
  }
  return 1;
}

StreamInfo::StreamInfo& FilterStateWrapper::streamInfo() { return parent_.stream_info_; }

int FilterStateWrapper::luaGet(lua_State* state) {
  const char* object_name = luaL_checkstring(state, 2);
  const StreamInfo::FilterStateSharedPtr filter_state = streamInfo().filterState();

  // Check if filter state exists.
  if (filter_state == nullptr) {
    return 0; // Return nil if filter state is null.
  }

  // Get the filter state object by name.
  const StreamInfo::FilterState::Object* object = filter_state->getDataReadOnlyGeneric(object_name);
  if (object == nullptr) {
    return 0; // Return nil if object not found.
  }

  // Check if there's an optional third parameter for field access.
  if (lua_gettop(state) >= 3 && !lua_isnil(state, 3)) {
    const char* field_name = luaL_checkstring(state, 3);
    if (object->hasFieldSupport()) {
      auto field_value = object->getField(field_name);

      // Convert the field value to the appropriate Lua type.
      if (absl::holds_alternative<absl::string_view>(field_value)) {
        const auto& str_value = absl::get<absl::string_view>(field_value);
        lua_pushlstring(state, str_value.data(), str_value.size());
        return 1;
      }

      if (absl::holds_alternative<int64_t>(field_value)) {
        lua_pushnumber(state, absl::get<int64_t>(field_value));
        return 1;
      }

      // Return nil if field is not found.
      return 0;
    }

    // Object doesn't support field access, return nil.
    return 0;
  }

  absl::optional<std::string> string_value = object->serializeAsString();
  if (string_value.has_value()) {
    const std::string& value = string_value.value();

    // Return the filter state value as a string.
    lua_pushlstring(state, value.data(), value.size());
    return 1;
  }

  // If string serialization is not supported, return nil.
  return 0;
}

const ProtobufWkt::Struct& VirtualHostWrapper::getMetadata() const {
  const auto& virtual_host = stream_info_.virtualHost();
  if (virtual_host == nullptr) {
    return ProtobufWkt::Struct::default_instance();
  }

  const auto& metadata = virtual_host->metadata();
  auto filter_it = metadata.filter_metadata().find(filter_config_name_);

  if (filter_it != metadata.filter_metadata().end()) {
    return filter_it->second;
  }

  return ProtobufWkt::Struct::default_instance();
}

int VirtualHostWrapper::luaMetadata(lua_State* state) {
  if (metadata_wrapper_.get() != nullptr) {
    metadata_wrapper_.pushStack();
  } else {
    metadata_wrapper_.reset(Filters::Common::Lua::MetadataMapWrapper::create(state, getMetadata()),
                            true);
  }
  return 1;
}

const ProtobufWkt::Struct& RouteWrapper::getMetadata() const {
  const auto& route = stream_info_.route();
  if (route == nullptr) {
    return ProtobufWkt::Struct::default_instance();
  }

  const auto& metadata = route->metadata();
  auto filter_it = metadata.filter_metadata().find(filter_config_name_);

  if (filter_it != metadata.filter_metadata().end()) {
    return filter_it->second;
  }

  return ProtobufWkt::Struct::default_instance();
}

int RouteWrapper::luaMetadata(lua_State* state) {
  if (metadata_wrapper_.get() != nullptr) {
    metadata_wrapper_.pushStack();
  } else {
    metadata_wrapper_.reset(Filters::Common::Lua::MetadataMapWrapper::create(state, getMetadata()),
                            true);
  }
  return 1;
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
