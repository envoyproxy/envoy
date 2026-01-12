#include <stddef.h>

#include "source/extensions/dynamic_modules/abi.h"

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(
  envoy_dynamic_module_type_server_factory_context_envoy_ptr server_factory_context_ptr) {
  return NULL;
}
