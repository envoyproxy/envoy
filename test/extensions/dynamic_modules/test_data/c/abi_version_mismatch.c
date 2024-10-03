#include "source/extensions/dynamic_modules/abi.h"

envoy_dynamic_module_type_abi_version envoy_dynamic_module_on_program_init() {
  return "invalid-version-hash";
}
