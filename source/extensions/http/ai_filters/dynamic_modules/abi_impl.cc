#include <string>

#include "envoy/http/header_map.h"

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
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::LLMProtocol;

DynamicModuleAiFilter& filterOf(envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr) {
  return *static_cast<DynamicModuleAiFilter*>(filter_envoy_ptr);
}

absl::string_view toStringView(envoy_dynamic_module_type_module_buffer buffer) {
  return buffer.ptr == nullptr ? absl::string_view() : absl::string_view(buffer.ptr, buffer.length);
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
  return ContextAccessor::getHeaderValue(
      makeOptRef<const Http::HeaderMap>(filterOf(filter_envoy_ptr).context().request_headers), key,
      result, index, total_count_out);
}

bool envoy_dynamic_module_callback_ai_filter_set_request_header(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty()) {
    return false;
  }
  filterOf(filter_envoy_ptr)
      .context()
      .request_headers.setCopy(Http::LowerCaseString(key_view), toStringView(value));
  return true;
}

bool envoy_dynamic_module_callback_ai_filter_remove_request_header(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty()) {
    return false;
  }
  filterOf(filter_envoy_ptr).context().request_headers.remove(Http::LowerCaseString(key_view));
  return true;
}

bool envoy_dynamic_module_callback_ai_filter_get_filter_state_bytes(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getFilterStateBytes(filterOf(filter_envoy_ptr).context().stream_info, key,
                                              result);
}

bool envoy_dynamic_module_callback_ai_filter_set_filter_state_bytes(
    envoy_dynamic_module_type_ai_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty()) {
    return false;
  }
  return ContextAccessor::setFilterStateBytes(filterOf(filter_envoy_ptr).context().stream_info,
                                              key_view, toStringView(value));
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
