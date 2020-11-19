#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(CommonWasmRestrictionTestCpp)

WASM_EXPORT(void, proxy_abi_version_0_2_1, (void)) {}

WASM_EXPORT(uint32_t, proxy_on_vm_start, (uint32_t context_id, uint32_t configuration_size)) {
  (void)(context_id);
  (void)(configuration_size);
  std::string level_message = "after on_vm_start, before proxy_log";
  proxy_log(LogLevel::info, level_message.c_str(), level_message.size());
  fprintf(stdout, "WASI write to stdout\n");
  return 1;
}

END_WASM_PLUGIN