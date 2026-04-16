#include "source/extensions/dynamic_modules/abi/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_tracer_config_module_ptr envoy_dynamic_module_on_tracer_config_new(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return NULL;
}

void envoy_dynamic_module_on_tracer_config_destroy(
    envoy_dynamic_module_type_tracer_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_tracer_span_module_ptr envoy_dynamic_module_on_tracer_start_span(
    envoy_dynamic_module_type_tracer_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer operation_name, bool traced,
    envoy_dynamic_module_type_trace_reason reason) {
  (void)config_module_ptr;
  (void)span_envoy_ptr;
  (void)operation_name;
  (void)traced;
  (void)reason;
  return NULL;
}

void envoy_dynamic_module_on_tracer_span_set_operation(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer operation) {
  (void)span_module_ptr;
  (void)operation;
}

void envoy_dynamic_module_on_tracer_span_set_tag(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_envoy_buffer value) {
  (void)span_module_ptr;
  (void)key;
  (void)value;
}

void envoy_dynamic_module_on_tracer_span_log(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr, int64_t timestamp_ns,
    envoy_dynamic_module_type_envoy_buffer event) {
  (void)span_module_ptr;
  (void)timestamp_ns;
  (void)event;
}

void envoy_dynamic_module_on_tracer_span_finish(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr) {
  (void)span_module_ptr;
}

void envoy_dynamic_module_on_tracer_span_inject_context(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr) {
  (void)span_module_ptr;
  (void)span_envoy_ptr;
}

envoy_dynamic_module_type_tracer_span_module_ptr
envoy_dynamic_module_on_tracer_span_spawn_child(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer name, int64_t start_time_ns) {
  (void)span_module_ptr;
  (void)name;
  (void)start_time_ns;
  return NULL;
}

void envoy_dynamic_module_on_tracer_span_set_sampled(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr, bool sampled) {
  (void)span_module_ptr;
  (void)sampled;
}

bool envoy_dynamic_module_on_tracer_span_use_local_decision(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr) {
  (void)span_module_ptr;
  return true;
}

bool envoy_dynamic_module_on_tracer_span_get_baggage(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key,
    envoy_dynamic_module_type_module_buffer* value_out) {
  (void)span_module_ptr;
  (void)key;
  value_out->ptr = NULL;
  value_out->length = 0;
  return false;
}

void envoy_dynamic_module_on_tracer_span_set_baggage(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_envoy_buffer value) {
  (void)span_module_ptr;
  (void)key;
  (void)value;
}

bool envoy_dynamic_module_on_tracer_span_get_trace_id(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_module_buffer* value_out) {
  (void)span_module_ptr;
  value_out->ptr = NULL;
  value_out->length = 0;
  return false;
}

bool envoy_dynamic_module_on_tracer_span_get_span_id(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_module_buffer* value_out) {
  (void)span_module_ptr;
  value_out->ptr = NULL;
  value_out->length = 0;
  return false;
}

void envoy_dynamic_module_on_tracer_span_destroy(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr) {
  (void)span_module_ptr;
}
