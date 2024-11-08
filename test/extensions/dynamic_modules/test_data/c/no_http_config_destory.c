#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_in_envoy_ptr envoy_dynamic_module_on_program_init() {
  return kAbiVersion;
}

envoy_dynamic_module_type_http_filter_config_in_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_in_envoy_ptr filter_config_envoy_ptr,
    const char* name_ptr, int name_size, const char* config_ptr, int config_size) {
  static int some_variable = 0;
  return &some_variable;
}
