#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {
extern "C" {

size_t getHeaderValueImpl(const Http::HeaderMap* map,
                          envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
                          envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
                          size_t* result_buffer_length_ptr, size_t index) {
  if (!map) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return 0;
  }
  absl::string_view key_view(key, key_length);
  // TODO: we might want to avoid copying the key here by trusting the key is already lower case.
  const auto values = map->get(Envoy::Http::LowerCaseString(key_view));
  if (index >= values.size()) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return values.size();
  }
  const auto& value = values[index]->value().getStringView();
  *result_buffer_ptr = const_cast<char*>(value.data());
  *result_buffer_length_ptr = value.size();
  return values.size();
}

bool headerAsAttribute(const Http::HeaderMap* map, const Envoy::Http::LowerCaseString& header,
                       envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
                       size_t* result_buffer_length_ptr) {
  if (!map) {
    return false;
  }
  auto lower_header = header.get();
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr = const_cast<char*>(lower_header.data());
  auto size = getHeaderValueImpl(map, key_ptr, lower_header.size(), result_buffer_ptr,
                                 result_buffer_length_ptr, 0);
  return size > 0;
}

size_t envoy_dynamic_module_callback_http_get_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->request_headers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

size_t envoy_dynamic_module_callback_http_get_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->request_trailers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

size_t envoy_dynamic_module_callback_http_get_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->response_headers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

size_t envoy_dynamic_module_callback_http_get_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->response_trailers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

bool setHeaderValueImpl(Http::HeaderMap* map, envoy_dynamic_module_type_buffer_module_ptr key,
                        size_t key_length, envoy_dynamic_module_type_buffer_module_ptr value,
                        size_t value_length) {
  if (!map) {
    return false;
  }
  absl::string_view key_view(key, key_length);
  if (value == nullptr) {
    map->remove(Envoy::Http::LowerCaseString(key_view));
    return true;
  }
  absl::string_view value_view(value, value_length);
  // TODO: we might want to avoid copying the key here by trusting the key is already lower case.
  map->setCopy(Envoy::Http::LowerCaseString(key_view), value_view);
  return true;
}

bool envoy_dynamic_module_callback_http_set_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->request_headers_, key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_set_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->request_trailers_, key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_set_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->response_headers_, key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_set_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->response_trailers_, key, key_length, value, value_length);
}

size_t envoy_dynamic_module_callback_http_get_request_headers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->request_headers_) {
    return 0;
  }
  return filter->request_headers_->size();
}

size_t envoy_dynamic_module_callback_http_get_request_trailers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->request_trailers_) {
    return 0;
  }
  return filter->request_trailers_->size();
}

size_t envoy_dynamic_module_callback_http_get_response_headers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->response_headers_) {
    return 0;
  }
  return filter->response_headers_->size();
}

size_t envoy_dynamic_module_callback_http_get_response_trailers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->response_trailers_) {
    return 0;
  }
  return filter->response_trailers_->size();
}

bool getHeadersImpl(const Http::HeaderMap* map,
                    envoy_dynamic_module_type_http_header* result_headers) {
  if (!map) {
    return false;
  }
  size_t i = 0;
  map->iterate([&i, &result_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

bool envoy_dynamic_module_callback_http_get_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->request_headers_, result_headers);
}

bool envoy_dynamic_module_callback_http_get_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->request_trailers_, result_headers);
}

bool envoy_dynamic_module_callback_http_get_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->response_headers_, result_headers);
}

bool envoy_dynamic_module_callback_http_get_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->response_trailers_, result_headers);
}

void envoy_dynamic_module_callback_http_send_response(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t status_code,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    envoy_dynamic_module_type_buffer_module_ptr body_ptr, size_t body_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  std::function<void(ResponseHeaderMap & headers)> modify_headers = nullptr;
  if (headers_vector != nullptr && headers_vector_size != 0) {
    modify_headers = [headers_vector, headers_vector_size](ResponseHeaderMap& headers) {
      for (size_t i = 0; i < headers_vector_size; i++) {
        const auto& header = &headers_vector[i];
        const absl::string_view key(static_cast<const char*>(header->key_ptr), header->key_length);
        const absl::string_view value(static_cast<const char*>(header->value_ptr),
                                      header->value_length);
        headers.addCopy(Http::LowerCaseString(key), value);
      }
    };
  }
  const absl::string_view body =
      body_ptr ? absl::string_view(static_cast<const char*>(body_ptr), body_length) : "";

  filter->sendLocalReply(static_cast<Http::Code>(status_code), body, modify_headers, 0,
                         "dynamic_module");
}

/**
 * Helper to get the metadata namespace from the stream info.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source the location of the metadata to use.
 * @param namespace_ptr is the namespace of the metadata.
 * @param namespace_length is the length of the namespace.
 * @return the metadata namespace if it exists, nullptr otherwise.
 */
const ProtobufWkt::Struct*
getMetadataNamespace(envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
                     envoy_dynamic_module_type_metadata_source metadata_source,
                     envoy_dynamic_module_type_buffer_module_ptr namespace_ptr,
                     size_t namespace_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (!callbacks) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "callbacks are not available");
    return nullptr;
  }
  auto& stream_info = callbacks->streamInfo();
  const envoy::config::core::v3::Metadata* metadata = nullptr;

  switch (metadata_source) {
  case envoy_dynamic_module_type_metadata_source_dynamic: {
    metadata = &stream_info.dynamicMetadata();
    break;
  }
  case envoy_dynamic_module_type_metadata_source_route: {
    auto route = stream_info.route();
    if (route) {
      metadata = &route->metadata();
    }
    break;
  }
  case envoy_dynamic_module_type_metadata_source_cluster: {
    auto clusterInfo = callbacks->clusterInfo();
    if (clusterInfo) {
      metadata = &clusterInfo->metadata();
    }
    break;
  }
  case envoy_dynamic_module_type_metadata_source_host: {
    auto upstreamInfo = stream_info.upstreamInfo();
    if (upstreamInfo) {
      auto hostInfo = upstreamInfo->upstreamHost();
      if (hostInfo) {
        auto md = hostInfo->metadata();
        if (md) {
          metadata = md.get();
        }
      }
    }
    break;
  }
  }
  if (!metadata) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "metadata is not available");
    return nullptr;
  }
  const auto& filter_metdata = metadata->filter_metadata();
  absl::string_view namespace_view(static_cast<const char*>(namespace_ptr), namespace_length);
  auto metadata_namespace = filter_metdata.find(namespace_view);
  if (metadata_namespace == filter_metdata.end()) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("namespace {} not found in metadata", namespace_view));
    return nullptr;
  }
  return &metadata_namespace->second;
}

/**
 * Helper to get the dynamic metadata namespace from the stream info. if the namespace does not
 * exist, it will be create, assuming stream info is available.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @return the metadata namespace if it exists, nullptr otherwise.
 */
ProtobufWkt::Struct*
getDynamicMetadataNamespace(envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
                            envoy_dynamic_module_type_buffer_module_ptr namespace_ptr,
                            size_t namespace_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return nullptr;
  }
  auto metadata = stream_info->dynamicMetadata().mutable_filter_metadata();
  absl::string_view namespace_view(static_cast<const char*>(namespace_ptr), namespace_length);
  auto metadata_namespace = metadata->find(namespace_view);
  if (metadata_namespace == metadata->end()) {
    metadata_namespace = metadata->emplace(namespace_view, ProtobufWkt::Struct{}).first;
  }
  return &metadata_namespace->second;
}

/**
 * Helper to get the metadata value from the metadata namespace.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @param key_ptr is the key of the dynamic metadata.
 * @param key_length is the length of the key.
 * @return the metadata value if it exists, nullptr otherwise.
 */
const ProtobufWkt::Value*
getMetadataValue(envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
                 envoy_dynamic_module_type_metadata_source metadata_source,
                 envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
                 envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length) {
  auto metadata_namespace =
      getMetadataNamespace(filter_envoy_ptr, metadata_source, namespace_ptr, namespace_length);
  if (!metadata_namespace) {
    return nullptr;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  auto key_metadata = metadata_namespace->fields().find(key_view);
  if (key_metadata == metadata_namespace->fields().end()) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("key {} not found in metadata namespace", key_view));
    return nullptr;
  }
  return &key_metadata->second;
}

bool envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double value) {
  auto metadata_namespace =
      getDynamicMetadataNamespace(filter_envoy_ptr, namespace_ptr, namespace_length);
  if (!metadata_namespace) {
    // If stream info is not available, we cannot guarantee that the namespace is created.
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  ProtobufWkt::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_number_value(value);
  metadata_namespace->MergeFrom(metadata_value);
  return true;
}

bool envoy_dynamic_module_callback_http_get_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double* result) {
  const auto key_metadata = getMetadataValue(filter_envoy_ptr, metadata_source, namespace_ptr,
                                             namespace_length, key_ptr, key_length);
  if (!key_metadata) {
    return false;
  }
  if (!key_metadata->has_number_value()) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
        fmt::format("key {} is not a number",
                    absl::string_view(static_cast<const char*>(key_ptr), key_length)));
    return false;
  }
  *result = key_metadata->number_value();
  return true;
}

bool envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length) {
  auto metadata_namespace =
      getDynamicMetadataNamespace(filter_envoy_ptr, namespace_ptr, namespace_length);
  if (!metadata_namespace) {
    // If stream info is not available, we cannot guarantee that the namespace is created.
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  absl::string_view value_view(static_cast<const char*>(value_ptr), value_length);
  ProtobufWkt::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_string_value(value_view);
  metadata_namespace->MergeFrom(metadata_value);
  return true;
}

bool envoy_dynamic_module_callback_http_get_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  const auto key_metadata = getMetadataValue(filter_envoy_ptr, metadata_source, namespace_ptr,
                                             namespace_length, key_ptr, key_length);
  if (!key_metadata) {
    return false;
  }
  if (!key_metadata->has_string_value()) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
        fmt::format("key {} is not a string",
                    absl::string_view(static_cast<const char*>(key_ptr), key_length)));
    return false;
  }
  const auto& value = key_metadata->string_value();
  *result = const_cast<char*>(value.data());
  *result_length = value.size();
  return true;
}

bool envoy_dynamic_module_callback_http_set_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  absl::string_view value_view(static_cast<const char*>(value_ptr), value_length);
  stream_info->filterState()->setData(key_view,
                                      std::make_unique<Router::StringAccessorImpl>(value_view),
                                      StreamInfo::FilterState::StateType::ReadOnly);
  return true;
}

bool envoy_dynamic_module_callback_http_get_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  auto filter_state = stream_info->filterState()->getDataReadOnly<Router::StringAccessor>(key_view);
  if (!filter_state) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("key not found in filter state", key_view));
    return false;
  }
  absl::string_view str = filter_state->asString();
  *result = const_cast<char*>(str.data());
  *result_length = str.size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_request_body_vector(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    buffer = filter->current_request_body_;
    if (!buffer) {
      return false;
    }
    // See the comment on current_request_body_ for when we reach this.
  }
  auto raw_slices = buffer->getRawSlices(std::nullopt);
  auto counter = 0;
  for (const auto& slice : raw_slices) {
    result_buffer_vector[counter].length = slice.len_;
    result_buffer_vector[counter].ptr = static_cast<char*>(slice.mem_);
    counter++;
  }
  return true;
}

bool envoy_dynamic_module_callback_http_get_request_body_vector_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    buffer = filter->current_request_body_;
    if (!buffer) {
      return false;
    }
    // See the comment on current_request_body_ for when we reach this line.
  }
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

bool envoy_dynamic_module_callback_http_append_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->decoder_callbacks_->decodingBuffer()) {
    if (filter->current_request_body_) { // See the comment on current_request_body_ for when we
                                         // enter this block.
      filter->current_request_body_->add(absl::string_view(static_cast<const char*>(data), length));
      return true;
    }
    return false;
  }
  filter->decoder_callbacks_->modifyDecodingBuffer([data, length](Buffer::Instance& buffer) {
    buffer.add(absl::string_view(static_cast<const char*>(data), length));
  });
  return true;
}

bool envoy_dynamic_module_callback_http_drain_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->decoder_callbacks_->decodingBuffer()) {
    if (filter->current_request_body_) { // See the comment on current_request_body_ for when we
                                         // enter this block.
      auto size = std::min<uint64_t>(filter->current_request_body_->length(), number_of_bytes);
      filter->current_request_body_->drain(size);
      return true;
    }
    return false;
  }

  filter->decoder_callbacks_->modifyDecodingBuffer([number_of_bytes](Buffer::Instance& buffer) {
    auto size = std::min<uint64_t>(buffer.length(), number_of_bytes);
    buffer.drain(size);
  });
  return true;
}

bool envoy_dynamic_module_callback_http_get_response_body_vector(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    buffer = filter->current_response_body_;
    if (!buffer) {
      return false;
    }
    // See the comment on current_response_body_ for when we reach this line.
  }
  auto raw_slices = buffer->getRawSlices(std::nullopt);
  auto counter = 0;
  for (const auto& slice : raw_slices) {
    result_buffer_vector[counter].length = slice.len_;
    result_buffer_vector[counter].ptr = static_cast<char*>(slice.mem_);
    counter++;
  }
  return true;
}

bool envoy_dynamic_module_callback_http_get_response_body_vector_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    buffer = filter->current_response_body_;
    if (!buffer) {
      return false;
    }
    // See the comment on current_response_body_ for when we reach this line.
  }
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

bool envoy_dynamic_module_callback_http_append_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->encoder_callbacks_->encodingBuffer()) {
    if (filter->current_response_body_) { // See the comment on current_response_body_ for when we
                                          // enter this block.
      filter->current_response_body_->add(
          absl::string_view(static_cast<const char*>(data), length));
      return true;
    }
    return false;
  }
  filter->encoder_callbacks_->modifyEncodingBuffer([data, length](Buffer::Instance& buffer) {
    buffer.add(absl::string_view(static_cast<const char*>(data), length));
  });
  return true;
}

bool envoy_dynamic_module_callback_http_drain_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (!filter->encoder_callbacks_->encodingBuffer()) {
    if (filter->current_response_body_) { // See the comment on current_response_body_ for when we
                                          // enter this block.
      auto size = std::min<uint64_t>(filter->current_response_body_->length(), number_of_bytes);
      filter->current_response_body_->drain(size);
      return true;
    }
    return false;
  }

  filter->encoder_callbacks_->modifyEncodingBuffer([number_of_bytes](Buffer::Instance& buffer) {
    auto size = std::min<uint64_t>(buffer.length(), number_of_bytes);
    buffer.drain(size);
  });
  return true;
}

void envoy_dynamic_module_callback_http_clear_route_cache(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  filter->decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
}

envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_callback_get_most_specific_route_config(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  const auto* config =
      Http::Utility::resolveMostSpecificPerFilterConfig<DynamicModuleHttpPerRouteFilterConfig>(
          filter->decoder_callbacks_);
  if (!config) {
    return nullptr;
  }
  return config->config_;
}

bool envoy_dynamic_module_callback_http_filter_get_attribute_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_RequestProtocol: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto protocol = stream_info->protocol();
      if (protocol.has_value()) {
        const auto& protocol_string_ref = Http::Utility::getProtocolString(protocol.value());
        *result = const_cast<char*>(protocol_string_ref.data());
        *result_length = protocol_string_ref.size();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamAddress: {
    const auto upstream_info = filter->upstreamInfo();
    if (upstream_info) {
      auto upstream_host = upstream_info->upstreamHost();
      if (upstream_host != nullptr && upstream_host->address() != nullptr) {
        auto addr = upstream_host->address()->asStringView();
        *result = const_cast<char*>(addr.data());
        *result_length = addr.size();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourceAddress: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto addressProvider =
          stream_info->downstreamAddressProvider().remoteAddress()->asStringView();
      *result = const_cast<char*>(addressProvider.data());
      *result_length = addressProvider.size();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationAddress: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto addressProvider =
          stream_info->downstreamAddressProvider().localAddress()->asStringView();
      *result = const_cast<char*>(addressProvider.data());
      *result_length = addressProvider.size();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestId: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      auto stream_id_provider = stream_info->getStreamIdProvider();
      if (stream_id_provider.has_value()) {
        const absl::optional<absl::string_view> request_id = stream_id_provider->toStringView();
        if (request_id.has_value()) {
          *result = const_cast<char*>(request_id->data());
          *result_length = request_id->size();
          ok = true;
        }
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestPath: {
    ok = headerAsAttribute(filter->request_headers_, Envoy::Http::Headers::get().Path, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestHost: {
    ok = headerAsAttribute(filter->request_headers_, Envoy::Http::Headers::get().Host, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestMethod: {
    ok = headerAsAttribute(filter->request_headers_, Envoy::Http::Headers::get().Method, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestScheme: {
    ok = headerAsAttribute(filter->request_headers_, Envoy::Http::Headers::get().Scheme, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestReferer: {
    ok = headerAsAttribute(filter->request_headers_, Envoy::Http::CustomHeaders::get().Referer,
                           result, result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestUserAgent: {
    ok = headerAsAttribute(filter->request_headers_, Envoy::Http::Headers::get().UserAgent, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestUrlPath: {
    const auto values = filter->request_headers_->get(Envoy::Http::Headers::get().Path);
    if (!values.empty()) {
      const auto& path = values[0]->value().getStringView();
      size_t query_offset = path.find('?');
      if (query_offset == absl::string_view::npos) {
        *result = const_cast<char*>(path.data());
        *result_length = path.size();
      } else {
        const auto url_path = path.substr(0, query_offset);
        *result = const_cast<char*>(url_path.data());
        *result_length = url_path.length();
      }
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestQuery: {
    const auto values = filter->request_headers_->get(Envoy::Http::Headers::get().Path);
    if (!values.empty()) {
      const auto& path = values[0]->value().getStringView();
      size_t query_offset = path.find('?');
      if (query_offset != absl::string_view::npos) {
        auto query = path.substr(query_offset + 1);
        const auto fragment_offset = query.find('#');
        query = query.substr(0, fragment_offset);
        *result = const_cast<char*>(query.data());
        *result_length = query.length();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsRouteName: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto& route_name = stream_info->getRouteName();
      *result = const_cast<char*>(route_name.data());
      *result_length = route_name.size();
      ok = true;
    }
    break;
  }
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
                        "Unsupported attribute ID {} as string",
                        static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool envoy_dynamic_module_callback_http_filter_get_attribute_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_ResponseCode: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto code = stream_info->responseCode();
      if (code.has_value()) {
        *result = code.value();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamPort: {
    const auto upstream_info = filter->upstreamInfo();
    if (upstream_info) {
      auto upstream_host = upstream_info->upstreamHost();
      if (upstream_host != nullptr && upstream_host->address() != nullptr) {
        auto ip = upstream_host->address()->ip();
        if (ip) {
          *result = ip->port();
          ok = true;
        }
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourcePort: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto ip = stream_info->downstreamAddressProvider().remoteAddress()->ip();
      if (ip) {
        *result = ip->port();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationPort: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto ip = stream_info->downstreamAddressProvider().localAddress()->ip();
      if (ip) {
        *result = ip->port();
        ok = true;
      }
    }
    break;
  }
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
                        "Unsupported attribute ID {} as int", static_cast<int64_t>(attribute_id));
  }
  return ok;
}

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_http_callout(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t callout_id,
    envoy_dynamic_module_type_buffer_module_ptr cluster_name, size_t cluster_name_length,
    envoy_dynamic_module_type_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_buffer_module_ptr body, size_t body_size,
    uint64_t timeout_milliseconds) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Try to get the cluster from the cluster manager for the given cluster name.
  absl::string_view cluster_name_view(cluster_name, cluster_name_length);

  // Construct the request message, starting with the headers, checking for required headers, and
  // adding the body if present.
  std::unique_ptr<RequestHeaderMapImpl> hdrs = Http::RequestHeaderMapImpl::create();
  for (size_t i = 0; i < headers_size; i++) {
    const auto& header = &headers[i];
    const absl::string_view key(static_cast<const char*>(header->key_ptr), header->key_length);
    const absl::string_view value(static_cast<const char*>(header->value_ptr),
                                  header->value_length);
    hdrs->addCopy(Http::LowerCaseString(key), value);
  }
  Http::RequestMessagePtr message(new Http::RequestMessageImpl(std::move(hdrs)));
  if (message->headers().Path() == nullptr || message->headers().Method() == nullptr ||
      message->headers().Host() == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }
  if (body_size > 0) {
    message->body().add(absl::string_view(static_cast<const char*>(body), body_size));
    message->headers().setContentLength(body_size);
  }
  return filter->sendHttpCallout(callout_id, cluster_name_view, std::move(message),
                                 timeout_milliseconds);
}
}
} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
