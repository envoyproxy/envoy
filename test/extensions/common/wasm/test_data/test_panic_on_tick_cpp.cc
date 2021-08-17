#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(PanicOnTick)

WASM_EXPORT(void, proxy_abi_version_0_2_1, (void)) {}

WASM_EXPORT(uint32_t, proxy_on_vm_start, (uint32_t, uint32_t)) { return 1; }

WASM_EXPORT(void, proxy_on_context_create, (uint32_t, uint32_t)) {}

static int* badptr = nullptr;

WASM_EXPORT(void, proxy_on_tick, (uint32_t)) { *badptr = 0; }

END_WASM_PLUGIN
