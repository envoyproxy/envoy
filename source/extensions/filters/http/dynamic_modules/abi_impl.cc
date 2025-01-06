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
}
} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
