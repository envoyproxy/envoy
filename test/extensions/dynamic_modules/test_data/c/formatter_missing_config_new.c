#include "source/extensions/dynamic_modules/abi/abi.h"


// This module is missing envoy_dynamic_module_on_formatter_config_new.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}
