#include <string>

#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"
#include "source/extensions/http/ai_filters/dynamic_modules/ai_filter.h"
#include "source/extensions/http/ai_filters/dynamic_modules/json_pointer.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {
namespace {

using Extensions::DynamicModules::ContextAccessor;
using Extensions::DynamicModules::HeadersMapOptConstRef;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::LLMProtocol;

DynamicModuleAiFilter& filterOf(envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr) {
  return *static_cast<DynamicModuleAiFilter*>(filter_envoy_ptr);
}

absl::string_view toStringView(envoy_dynamic_module_type_module_buffer buffer) {
  return buffer.ptr == nullptr ? absl::string_view() : absl::string_view(buffer.ptr, buffer.length);
}

// The header map asserts that names and values hold no CR, LF or NUL, and an HTTP/1 upstream would
// read one as the start of another header, so module bytes are checked before they reach it.
bool isValidHeaderName(absl::string_view name) {
  return !name.empty() && Http::HeaderUtility::headerNameIsValid(name);
}

envoy_dynamic_module_type_ai_filter_json_result toJsonResult(const absl::Status& status) {
  switch (status.code()) {
  case absl::StatusCode::kOk:
    return envoy_dynamic_module_type_ai_filter_json_result_Ok;
  case absl::StatusCode::kNotFound:
    return envoy_dynamic_module_type_ai_filter_json_result_NotFound;
  case absl::StatusCode::kFailedPrecondition:
    return envoy_dynamic_module_type_ai_filter_json_result_Offloaded;
  default:
    return envoy_dynamic_module_type_ai_filter_json_result_InvalidArgument;
  }
}

} // namespace

extern "C" {

envoy_dynamic_module_type_ai_filter_json_result
envoy_dynamic_module_callback_ai_filter_get_request_json(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer json_pointer,
    envoy_dynamic_module_type_envoy_buffer* result) {
  DynamicModuleAiFilter& filter = filterOf(filter_envoy_ptr);
  const AiRequest* request = filter.request();
  if (request == nullptr) {
    return envoy_dynamic_module_type_ai_filter_json_result_Unavailable;
  }
  absl::StatusOr<std::string> value = getJson(request->json(), toStringView(json_pointer));
  if (!value.ok()) {
    return toJsonResult(value.status());
  }
  std::string& json_result = filter.jsonResult();
  json_result = *std::move(value);
  *result = {json_result.data(), json_result.size()};
  return envoy_dynamic_module_type_ai_filter_json_result_Ok;
}

envoy_dynamic_module_type_ai_filter_json_result
envoy_dynamic_module_callback_ai_filter_set_request_json(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer json_pointer,
    envoy_dynamic_module_type_module_buffer value) {
  AiRequest* request = filterOf(filter_envoy_ptr).request();
  if (request == nullptr) {
    return envoy_dynamic_module_type_ai_filter_json_result_Unavailable;
  }
  return toJsonResult(setJson(request->json(), toStringView(json_pointer), toStringView(value)));
}

envoy_dynamic_module_type_ai_filter_json_result
envoy_dynamic_module_callback_ai_filter_remove_request_json(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer json_pointer) {
  AiRequest* request = filterOf(filter_envoy_ptr).request();
  if (request == nullptr) {
    return envoy_dynamic_module_type_ai_filter_json_result_Unavailable;
  }
  return toJsonResult(removeJson(request->json(), toStringView(json_pointer)));
}

bool envoy_dynamic_module_callback_ai_filter_get_request_protocol(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const AiRequest* request = filterOf(filter_envoy_ptr).request();
  if (request == nullptr || request->protocol() == LLMProtocol::Unspecified) {
    return false;
  }
  const absl::string_view name =
      HttpFilters::AiProtocolManager::llmProtocolName(request->protocol());
  *result = {name.data(), name.size()};
  return true;
}

bool envoy_dynamic_module_callback_ai_filter_get_request_header_value(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  const AiFilterContext* context = filterOf(filter_envoy_ptr).context();
  HeadersMapOptConstRef headers;
  if (context != nullptr && isValidHeaderName(toStringView(key))) {
    headers = makeOptRef<const Http::HeaderMap>(context->request_headers);
  }
  return ContextAccessor::getHeaderValue(headers, key, result, index, total_count_out);
}

bool envoy_dynamic_module_callback_ai_filter_set_request_header(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  const AiFilterContext* context = filterOf(filter_envoy_ptr).context();
  const absl::string_view name = toStringView(key);
  const absl::string_view value_view = toStringView(value);
  if (context == nullptr || !isValidHeaderName(name) ||
      !Http::HeaderUtility::headerValueIsValid(value_view)) {
    return false;
  }
  context->request_headers.setCopy(Http::LowerCaseString(name), value_view);
  return true;
}

bool envoy_dynamic_module_callback_ai_filter_remove_request_header(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key) {
  const AiFilterContext* context = filterOf(filter_envoy_ptr).context();
  const absl::string_view name = toStringView(key);
  if (context == nullptr || !isValidHeaderName(name)) {
    return false;
  }
  context->request_headers.remove(Http::LowerCaseString(name));
  return true;
}

bool envoy_dynamic_module_callback_ai_filter_get_filter_state_bytes(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  const AiFilterContext* context = filterOf(filter_envoy_ptr).context();
  if (context == nullptr) {
    return false;
  }
  return ContextAccessor::getFilterStateBytes(context->stream_info, key, result);
}

bool envoy_dynamic_module_callback_ai_filter_set_filter_state_bytes(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  const AiFilterContext* context = filterOf(filter_envoy_ptr).context();
  const absl::string_view key_view = toStringView(key);
  if (context == nullptr || key_view.empty()) {
    return false;
  }
  return ContextAccessor::setFilterStateBytes(context->stream_info, key_view, toStringView(value));
}

bool envoy_dynamic_module_callback_ai_filter_send_local_reply(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr, uint32_t status_code,
    envoy_dynamic_module_type_module_buffer body) {
  if (status_code < 200 || status_code > 599) {
    return false;
  }
  return filterOf(filter_envoy_ptr)
      .sendLocalReply(static_cast<Http::Code>(status_code), toStringView(body));
}

} // extern "C"

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
