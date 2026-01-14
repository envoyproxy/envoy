#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

// Other functions that will be accessed by other tests.
int dynamicModulesTestLoadGlobally();

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return kAbiVersion;
}

int getSomeVariable(void) { return dynamicModulesTestLoadGlobally(); }
