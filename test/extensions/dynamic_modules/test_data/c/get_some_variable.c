#include "source/extensions/dynamic_modules/abi.h"

envoy_dynamic_module_type_program_init_result envoy_dynamic_module_on_program_init() { return 0; }

int getSomeVariable() {
  static int some_variable = 0;
  some_variable++;
  return some_variable;
}
