#include <stddef.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init() {
  return kAbiVersion;
}

static int some_variable = 0;

int getSomeVariable(void) {
  some_variable++;
  return some_variable;
}