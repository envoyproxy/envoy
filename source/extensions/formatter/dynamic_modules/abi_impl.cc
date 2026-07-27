#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/formatter/dynamic_modules/formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::HeadersMapOptConstRef;

extern "C" {

// -----------------------------------------------------------------------------
// Formatter Callbacks - Headers
// -----------------------------------------------------------------------------

size_t envoy_dynamic_module_callback_formatter_get_headers_size(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  HeadersMapOptConstRef map = ContextAccessor::headerMapByType(*ctx->context, header_type);
  return map.has_value() ? map->size() : 0;
}

bool envoy_dynamic_module_callback_formatter_get_headers(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getHeaders(ContextAccessor::headerMapByType(*ctx->context, header_type),
                                     result_headers);
}

bool envoy_dynamic_module_callback_formatter_get_header_value(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getHeaderValue(
      ContextAccessor::headerMapByType(*ctx->context, header_type), key, result, index,
      total_count_out);
}

// -----------------------------------------------------------------------------
// Formatter Callbacks - Generic Attributes
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_formatter_get_attribute_string(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getAttributeString(*ctx->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_formatter_get_attribute_int(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getAttributeInt(*ctx->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_formatter_get_attribute_bool(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getAttributeBool(*ctx->stream_info, attribute_id, result);
}

// -----------------------------------------------------------------------------
// Formatter Callbacks - Metadata and Dynamic State
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_formatter_get_dynamic_metadata(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getDynamicMetadata(*ctx->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_formatter_get_local_reply_body(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::getLocalReplyBody(*ctx->context, result);
}

envoy_dynamic_module_type_access_log_type
envoy_dynamic_module_callback_formatter_get_access_log_type(
    envoy_dynamic_module_type_formatter_context_envoy_ptr formatter_context_envoy_ptr) {
  auto* ctx = static_cast<FormatterContext*>(formatter_context_envoy_ptr);
  return ContextAccessor::accessLogTypeToAbi(ctx->context->accessLogType());
}

} // extern "C"

} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
