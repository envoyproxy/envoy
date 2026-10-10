#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static uint64_t memory_usage = 1234;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

uint64_t envoy_dynamic_module_on_program_get_memory_usage(void) { return memory_usage; }

void set_memory_usage(uint64_t bytes) { memory_usage = bytes; }
