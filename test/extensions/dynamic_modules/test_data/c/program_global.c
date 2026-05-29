#include <assert.h>

#include "source/extensions/dynamic_modules/abi/abi.h"


envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

// Other functions that will be accessed by other tests.
int dynamicModulesTestLoadGlobally() {
  return 42;
}
