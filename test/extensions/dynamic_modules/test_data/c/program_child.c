#include <assert.h>

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

// This function is also defined in program_global.c. When program_global is loaded with
// RTLD_GLOBAL before this module, calling this function from getSomeVariable() exercises
// the symbol resolution path. This stub provides a fallback for linking.
int dynamicModulesTestLoadGlobally(void) { return 42; }

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(
  envoy_dynamic_module_type_server_factory_context_envoy_ptr server_factory_context_ptr) {
  return kAbiVersion;
}

int getSomeVariable(void) { return dynamicModulesTestLoadGlobally(); }
