// NOLINT(namespace-envoy)
#include "proxy_wasm_intrinsics.h"

// Required Proxy-Wasm ABI version.
extern "C" PROXY_WASM_KEEPALIVE void proxy_abi_version_0_1_0() {}

extern "C" void missing();

extern "C" PROXY_WASM_KEEPALIVE uint32_t proxy_on_vm_start(uint32_t, uint32_t) {
  missing();
  return 1;
}
