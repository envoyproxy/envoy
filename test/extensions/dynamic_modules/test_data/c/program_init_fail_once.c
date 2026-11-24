#include <stdatomic.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

static atomic_int init_calls;

// Fails the first init, so that a test can load the module again after a failed init.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return atomic_fetch_add(&init_calls, 1) == 0 ? NULL : envoy_dynamic_modules_abi_version;
}

int get_init_calls(void) { return atomic_load(&init_calls); }
