#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

static int some_variable = 0;

int getSomeVariable(void) {
  some_variable++;
  return some_variable;
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    const char* name_ptr, size_t name_size, const char* config_ptr, size_t config_size) {
  return &some_variable;
}

void envoy_dynamic_module_on_http_filter_config_destroy(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr) {
  assert(filter_config_ptr == &some_variable);
}

envoy_dynamic_module_type_http_filter_module_ptr envoy_dynamic_module_on_http_filter_new(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  return &some_variable + 1;
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
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  assert(filter_module_ptr == &some_variable + 1);
}

void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  assert(filter_module_ptr == &some_variable + 1);
}

void envoy_dynamic_module_on_http_filter_http_callout_done(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint32_t callout_id,
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
