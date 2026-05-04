#include "source/extensions/dynamic_modules/abi/abi.h"

// Reverse tunnel reporter dynamic module that omits the required
// _new and _destroy hooks. Used to exercise the factory's missing-
// required-hooks failure path.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

void envoy_dynamic_module_on_reverse_tunnel_server_initialized(
    envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr reporter_ptr) {
  (void)reporter_ptr;
}
