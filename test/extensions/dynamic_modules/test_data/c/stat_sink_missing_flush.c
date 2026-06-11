#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Stats sink module missing on_stat_sink_flush.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_stat_sink_config_module_ptr
envoy_dynamic_module_on_stat_sink_config_new(
    envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer sink_name,
    envoy_dynamic_module_type_envoy_buffer sink_config) {
  (void)config_envoy_ptr;
  (void)sink_name;
  (void)sink_config;
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_stat_sink_config_destroy(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

void envoy_dynamic_module_on_stat_sink_on_histogram_complete(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer histogram_name, uint64_t value) {
  (void)config_module_ptr;
  (void)histogram_name;
  (void)value;
}
