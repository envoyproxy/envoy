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
  std::string log_message = "after proxy_on_vm_start: written by proxy_log";
  proxy_log(LogLevel::info, log_message.c_str(), log_message.size());
  fprintf(stdout, "WASI write to stdout\n");
  return 1;
}

WASM_EXPORT(void, proxy_on_context_create, (uint32_t context_id, uint32_t parent_context_id)) {
  (void)(context_id);
  (void)(parent_context_id);
  std::string log_message = "after proxy_on_context_create: written by proxy_log";
  proxy_log(LogLevel::info, log_message.c_str(), log_message.size());
}

END_WASM_PLUGIN
