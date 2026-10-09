#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module exercises the mutable and read-only halves of the early header mutation callback
// surface: it reads a header, echoes it under a new key, builds a multi-value header with
// add_header, removes a header, and reads a stream info attribute.

static envoy_dynamic_module_type_module_buffer str_buf(const char* s) {
  envoy_dynamic_module_type_module_buffer buf;
  buf.ptr = s;
  buf.length = strlen(s);
  return buf;
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_early_header_mutation_config_module_ptr
envoy_dynamic_module_on_early_header_mutation_config_new(
    envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_early_header_mutation_config_destroy(
    envoy_dynamic_module_type_early_header_mutation_config_module_ptr config_module_ptr) {}

bool envoy_dynamic_module_on_early_header_mutation_mutate(
    envoy_dynamic_module_type_early_header_mutation_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr) {
  // Read a single header value and echo it back under a different key.
  envoy_dynamic_module_type_envoy_buffer value;
  size_t total_count = 0;
  if (envoy_dynamic_module_callback_early_header_mutation_get_header_value(
          envoy_ptr, str_buf("x-test-input"), &value, 0, &total_count)) {
    envoy_dynamic_module_type_module_buffer echoed;
    echoed.ptr = value.ptr;
    echoed.length = value.length;
    envoy_dynamic_module_callback_early_header_mutation_set_header(
        envoy_ptr, str_buf("x-dynamic-module-echo"), echoed);
  }

  // Build a multi-value header, then report the value count that get_header_value observes.
  envoy_dynamic_module_callback_early_header_mutation_add_header(
      envoy_ptr, str_buf("x-dynamic-module-added"), str_buf("one"));
  envoy_dynamic_module_callback_early_header_mutation_add_header(
      envoy_ptr, str_buf("x-dynamic-module-added"), str_buf("two"));
  total_count = 0;
  if (envoy_dynamic_module_callback_early_header_mutation_get_header_value(
          envoy_ptr, str_buf("x-dynamic-module-added"), &value, 1, &total_count)) {
    envoy_dynamic_module_callback_early_header_mutation_set_header(
        envoy_ptr, str_buf("x-dynamic-module-multi"),
        total_count == 2 ? str_buf("2") : str_buf("unexpected"));
  }

  // Remove a header and confirm it is gone.
  envoy_dynamic_module_callback_early_header_mutation_remove_header(envoy_ptr,
                                                                    str_buf("x-remove-me"));
  const bool still_present = envoy_dynamic_module_callback_early_header_mutation_get_header_value(
      envoy_ptr, str_buf("x-remove-me"), &value, 0, NULL);
  envoy_dynamic_module_callback_early_header_mutation_set_header(
      envoy_ptr, str_buf("x-dynamic-module-removed"),
      still_present ? str_buf("no") : str_buf("yes"));

  // Read a connection-level attribute, which is populated this early.
  uint64_t connection_id = 0;
  if (envoy_dynamic_module_callback_early_header_mutation_get_attribute_int(
          envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionId, &connection_id)) {
    envoy_dynamic_module_callback_early_header_mutation_set_header(
        envoy_ptr, str_buf("x-dynamic-module-connection-id-present"), str_buf("yes"));
  }

  return true;
}
