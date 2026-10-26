#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module marks the request and then stops the early header mutation chain, so a test can
// assert that the extensions configured after it do not run.

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
  envoy_dynamic_module_callback_early_header_mutation_set_header(
      envoy_ptr, str_buf("x-dynamic-module-first"), str_buf("ran"));
  // Stop the chain: the mutations above are kept, but no later extension runs.
  return false;
}
