#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_in_envoy_ptr envoy_dynamic_module_on_program_init() {
  return kAbiVersion;
}
