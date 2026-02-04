#include <assert.h>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

void envoy_dynamic_module_on_http_filter_per_route_config_destroy(
    envoy_dynamic_module_type_http_filter_per_route_config_module_ptr filter_config_ptr) {}

envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_on_http_filter_per_route_config_new(
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  return 0;
}
