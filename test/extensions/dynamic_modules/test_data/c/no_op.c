#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

static int some_variable = 0;

int getSomeVariable() {
  some_variable++;
  return some_variable;
}

envoy_dynamic_module_type_abi_version_envoy_ptr envoy_dynamic_module_on_program_init() {
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
