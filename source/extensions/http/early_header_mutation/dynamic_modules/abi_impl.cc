#include "envoy/http/header_map.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/http/early_header_mutation/dynamic_modules/early_header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::HeadersMapOptConstRef;

namespace {

EarlyHeaderMutationContext*
mutationContext(envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr) {
  return static_cast<EarlyHeaderMutationContext*>(envoy_ptr);
}

// The request header map is the only map that exists at this point, and it is always present, so
// the read helpers can be handed it unconditionally.
HeadersMapOptConstRef requestHeaders(const EarlyHeaderMutationContext* context) {
  return makeOptRef<const ::Envoy::Http::HeaderMap>(context->headers);
}

// Returns an empty string_view for a null buffer so an empty key is rejected uniformly.
absl::string_view toStringView(envoy_dynamic_module_type_module_buffer buffer) {
  if (buffer.ptr == nullptr || buffer.length == 0) {
    return {};
  }
  return absl::string_view(buffer.ptr, buffer.length);
}

} // namespace

extern "C" {

// -----------------------------------------------------------------------------
// Early Header Mutation Callbacks - Headers
// -----------------------------------------------------------------------------

size_t envoy_dynamic_module_callback_early_header_mutation_get_headers_size(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr) {
  return mutationContext(envoy_ptr)->headers.size();
}

bool envoy_dynamic_module_callback_early_header_mutation_get_headers(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  return ContextAccessor::getHeaders(requestHeaders(mutationContext(envoy_ptr)), result_headers);
}

bool envoy_dynamic_module_callback_early_header_mutation_get_header_value(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  return ContextAccessor::getHeaderValue(requestHeaders(mutationContext(envoy_ptr)), key, result,
                                         index, total_count_out);
}

bool envoy_dynamic_module_callback_early_header_mutation_set_header(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty()) {
    return false;
  }
  mutationContext(envoy_ptr)->headers.setCopy(::Envoy::Http::LowerCaseString(key_view),
                                              toStringView(value));
  return true;
}

bool envoy_dynamic_module_callback_early_header_mutation_add_header(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty()) {
    return false;
  }
  mutationContext(envoy_ptr)->headers.addCopy(::Envoy::Http::LowerCaseString(key_view),
                                              toStringView(value));
  return true;
}

bool envoy_dynamic_module_callback_early_header_mutation_remove_header(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer key) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty()) {
    return false;
  }
  mutationContext(envoy_ptr)->headers.remove(::Envoy::Http::LowerCaseString(key_view));
  return true;
}

// -----------------------------------------------------------------------------
// Early Header Mutation Callbacks - Generic Attributes
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_early_header_mutation_get_attribute_string(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getAttributeString(mutationContext(envoy_ptr)->stream_info, attribute_id,
                                             result);
}

bool envoy_dynamic_module_callback_early_header_mutation_get_attribute_int(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  return ContextAccessor::getAttributeInt(mutationContext(envoy_ptr)->stream_info, attribute_id,
                                          result);
}

bool envoy_dynamic_module_callback_early_header_mutation_get_attribute_bool(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  return ContextAccessor::getAttributeBool(mutationContext(envoy_ptr)->stream_info, attribute_id,
                                           result);
}

// -----------------------------------------------------------------------------
// Early Header Mutation Callbacks - Metadata and Dynamic State
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getDynamicMetadata(mutationContext(envoy_ptr)->stream_info, filter_name,
                                             path, result);
}

bool envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, double* result) {
  return ContextAccessor::getDynamicMetadataNumber(mutationContext(envoy_ptr)->stream_info,
                                                   filter_name, path, result);
}

bool envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_bool(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, bool* result) {
  return ContextAccessor::getDynamicMetadataBool(mutationContext(envoy_ptr)->stream_info,
                                                 filter_name, path, result);
}

bool envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getFilterStateBytes(mutationContext(envoy_ptr)->stream_info, key, result);
}

} // extern "C"

} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
