#include "source/extensions/dynamic_modules/abi.h"

envoy_dynamic_module_type_program_init_result envoy_dynamic_module_on_program_init() {
  return 12345;
}
