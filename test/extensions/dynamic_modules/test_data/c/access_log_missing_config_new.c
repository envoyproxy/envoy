#include <assert.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"


// This module is missing all access logger configuration symbols except program init.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}
