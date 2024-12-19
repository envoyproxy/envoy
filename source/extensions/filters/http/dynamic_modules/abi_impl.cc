#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"

extern "C" {

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

size_t getHeaderValueImpl(const Http::HeaderMap* map,
                          envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
                          envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
                          size_t* result_buffer_length_ptr, size_t index) {
  if (map == nullptr) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return 0;
  }
  absl::string_view key_view(key, key_length);
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

size_t envoy_dynamic_module_callback_http_get_request_header_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // TODO(mathetake): maybe get the headers map from decoder/encoder callbacks?
  return getHeaderValueImpl(filter->request_headers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

size_t envoy_dynamic_module_callback_http_get_request_trailer_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // TODO(mathetake): maybe get the headers map from decoder/encoder callbacks?
  return getHeaderValueImpl(filter->request_trailers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

size_t envoy_dynamic_module_callback_http_get_response_header_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // TODO(mathetake): maybe get the headers map from decoder/encoder callbacks?
  return getHeaderValueImpl(filter->response_headers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

size_t envoy_dynamic_module_callback_http_get_response_trailer_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // TODO(mathetake): maybe get the headers map from decoder/encoder callbacks?
  return getHeaderValueImpl(filter->response_trailers_, key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index);
}

} // namespace HttpFilters

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
} // namespace Envoy
