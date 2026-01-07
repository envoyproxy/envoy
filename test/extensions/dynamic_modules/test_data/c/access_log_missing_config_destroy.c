#include <assert.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

envoy_dynamic_module_type_access_logger_config_module_ptr
envoy_dynamic_module_on_access_logger_config_new(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    const char* name_ptr, size_t name_size,
    const char* config_ptr, size_t config_size) {
  static int config_dummy = 0;
  return &config_dummy;
}
