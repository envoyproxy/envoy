#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  // Returning null here causes the filter config initialization to fail, which the factory
  // surfaces as a ``dynamic_modules.config_init_error`` stat. All other required symbols are
  // present so that symbol resolution succeeds and the failure is isolated to config init.
  return 0;
}

void envoy_dynamic_module_on_http_filter_config_destroy(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr) {}

envoy_dynamic_module_type_http_filter_module_ptr envoy_dynamic_module_on_http_filter_new(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  // Never called: on_http_filter_config_new returns null, so no filter instance is ever created.
  return 0;
}

envoy_dynamic_module_type_on_http_filter_request_headers_status
envoy_dynamic_module_on_http_filter_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  return envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue;
}

envoy_dynamic_module_type_on_http_filter_request_body_status
envoy_dynamic_module_on_http_filter_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  return envoy_dynamic_module_type_on_http_filter_request_body_status_Continue;
}

envoy_dynamic_module_type_on_http_filter_request_trailers_status
envoy_dynamic_module_on_http_filter_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  return envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue;
}

envoy_dynamic_module_type_on_http_filter_response_headers_status
envoy_dynamic_module_on_http_filter_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  return envoy_dynamic_module_type_on_http_filter_response_headers_status_Continue;
}

envoy_dynamic_module_type_on_http_filter_response_body_status
envoy_dynamic_module_on_http_filter_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  return envoy_dynamic_module_type_on_http_filter_response_body_status_Continue;
}

envoy_dynamic_module_type_on_http_filter_response_trailers_status
envoy_dynamic_module_on_http_filter_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  return envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue;
}

void envoy_dynamic_module_on_http_filter_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {}

void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {}

void envoy_dynamic_module_on_http_filter_http_callout_done(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_vector, size_t body_vector_size) {}

void envoy_dynamic_module_on_http_filter_scheduled(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t event_id) {}

void envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {}

void envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {}

envoy_dynamic_module_type_on_http_filter_local_reply_status
envoy_dynamic_module_on_http_filter_local_reply(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint32_t response_code,
    envoy_dynamic_module_type_envoy_buffer details, bool reset_imminent) {
  return envoy_dynamic_module_type_on_http_filter_local_reply_status_Continue;
}

void envoy_dynamic_module_on_http_filter_http_stream_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_handle,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size, bool end_stream) {}

void envoy_dynamic_module_on_http_filter_http_stream_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_handle,
    const envoy_dynamic_module_type_envoy_buffer* data, size_t data_count, bool end_stream) {}

void envoy_dynamic_module_on_http_filter_http_stream_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_handle,
    envoy_dynamic_module_type_envoy_http_header* trailers, size_t trailers_size) {}

void envoy_dynamic_module_on_http_filter_http_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_handle) {}

void envoy_dynamic_module_on_http_filter_http_stream_reset(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_handle,
    envoy_dynamic_module_type_http_stream_reset_reason reset_reason) {}

void envoy_dynamic_module_on_http_filter_config_http_callout_done(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {}

void envoy_dynamic_module_on_http_filter_config_http_stream_headers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size, bool end_stream) {}

void envoy_dynamic_module_on_http_filter_config_http_stream_data(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    const envoy_dynamic_module_type_envoy_buffer* data, size_t data_count, bool end_stream) {}

void envoy_dynamic_module_on_http_filter_config_http_stream_trailers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* trailers, size_t trailers_size) {}

void envoy_dynamic_module_on_http_filter_config_http_stream_complete(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id) {
}

void envoy_dynamic_module_on_http_filter_config_http_stream_reset(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_http_stream_reset_reason reason) {}
