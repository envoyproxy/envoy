#include "source/common/http/header_utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/matching/input_matchers/dynamic_modules/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

namespace {

using HeadersMapOptConstRef = OptRef<const ::Envoy::Http::HeaderMap>;

HeadersMapOptConstRef getHeaderMapByType(const MatchContext* context,
                                         envoy_dynamic_module_type_http_header_type header_type) {
  switch (header_type) {
  case envoy_dynamic_module_type_http_header_type_RequestHeader:
    return makeOptRefFromPtr<const ::Envoy::Http::HeaderMap>(context->request_headers);
  case envoy_dynamic_module_type_http_header_type_ResponseHeader:
    return makeOptRefFromPtr<const ::Envoy::Http::HeaderMap>(context->response_headers);
  case envoy_dynamic_module_type_http_header_type_ResponseTrailer:
    return makeOptRefFromPtr<const ::Envoy::Http::HeaderMap>(context->response_trailers);
  default:
    return {};
  }
}

bool getHeaderValueImpl(HeadersMapOptConstRef map, envoy_dynamic_module_type_module_buffer key,
                        envoy_dynamic_module_type_envoy_buffer* result, size_t index,
                        size_t* optional_size) {
  if (!map.has_value()) {
    *result = {.ptr = nullptr, .length = 0};
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  const auto values = map->get(::Envoy::Http::LowerCaseString(key_view));
  if (optional_size != nullptr) {
    *optional_size = values.size();
  }

  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

bool getHeadersImpl(HeadersMapOptConstRef map,
                    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  if (!map) {
    return false;
  }
  size_t i = 0;
  map->iterate([&i, &result_headers](
                   const ::Envoy::Http::HeaderEntry& header) -> ::Envoy::Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return ::Envoy::Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

} // namespace

} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy

extern "C" {

size_t envoy_dynamic_module_callback_matcher_get_headers_size(
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type) {
  using namespace Envoy::Extensions::Matching::InputMatchers::DynamicModules;
  auto* context = static_cast<MatchContext*>(matcher_input_envoy_ptr);
  auto map = getHeaderMapByType(context, header_type);
  return map.has_value() ? map->size() : 0;
}

bool envoy_dynamic_module_callback_matcher_get_headers(
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  using namespace Envoy::Extensions::Matching::InputMatchers::DynamicModules;
  auto* context = static_cast<MatchContext*>(matcher_input_envoy_ptr);
  auto map = getHeaderMapByType(context, header_type);
  return getHeadersImpl(map, result_headers);
}

bool envoy_dynamic_module_callback_matcher_get_header_value(
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  using namespace Envoy::Extensions::Matching::InputMatchers::DynamicModules;
  auto* context = static_cast<MatchContext*>(matcher_input_envoy_ptr);
  auto map = getHeaderMapByType(context, header_type);
  return getHeaderValueImpl(map, key, result, index, total_count_out);
}

} // extern "C"
