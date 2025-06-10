#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_envoy_ptr envoy_dynamic_module_on_program_init() {
  return kAbiVersion;
}

void envoy_dynamic_module_on_http_filter_per_route_config_destroy(
    envoy_dynamic_module_type_http_filter_per_route_config_module_ptr filter_config_ptr){
}

envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_on_http_filter_per_route_config_new(const char* name_ptr, size_t name_size,
                                                         const char* config_ptr,
                                                         size_t config_size) {
  return 0;
}
