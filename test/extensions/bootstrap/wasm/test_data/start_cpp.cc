// NOLINT(namespace-envoy)
#include <string>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(WasmStartCpp)

// Required Proxy-Wasm ABI version.
WASM_EXPORT(void, proxy_abi_version_0_1_0, ()) {}

WASM_EXPORT(uint32_t, proxy_on_vm_start, (uint32_t, uint32_t configuration_size)) {
  logDebug("onStart");
  return configuration_size ? 0 /* failure */ : 1 /* success */;
}

WASM_EXPORT(uint32_t, proxy_on_configure, (uint32_t, uint32_t configuration_size)) {
  // Fail if we are provided a non-empty configuration.
  return configuration_size ? 0 /* failure */ : 1 /* success */;
}

END_WASM_PLUGIN
