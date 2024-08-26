#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

int getSomeVariable() {
  static int some_variable = 0;
  some_variable++;
  return some_variable;
}

envoy_dynamic_module_type_abi_version envoy_dynamic_module_on_program_init() { return kAbiVersion; }
