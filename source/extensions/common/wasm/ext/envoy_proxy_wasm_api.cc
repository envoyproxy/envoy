// NOLINT(namespace-envoy)

#include "proxy_wasm_intrinsics.h"

/*
 * These headers span repositories and therefor the following header can not include the above
 * header to enforce the required order. This macros prevent header reordering.
 */
#define _THE_FOLLOWING_INCLUDE_MUST_COME_AFTER_THOSE_ABOVE_ 1
#include "source/extensions/common/wasm/ext/envoy_proxy_wasm_api.h"
#undef _THE_FOLLOWING_INCLUDE_MUST_COME_AFTER_THOSE_ABOVE_

EnvoyContextBase* getEnvoyContextBase(uint32_t context_id) {
  auto context_base = getContextBase(context_id);
  if (auto root = context_base->asRoot()) {
    return static_cast<EnvoyContextBase*>(static_cast<EnvoyRootContext*>(root));
  } else {
    return static_cast<EnvoyContextBase*>(static_cast<EnvoyContext*>(context_base->asContext()));
  }
}

EnvoyContext* getEnvoyContext(uint32_t context_id) {
  auto context_base = getContextBase(context_id);
  return static_cast<EnvoyContext*>(context_base->asContext());
}

EnvoyRootContext* getEnvoyRootContext(uint32_t context_id) {
  auto context_base = getContextBase(context_id);
  return static_cast<EnvoyRootContext*>(context_base->asRoot());
}

extern "C" PROXY_WASM_KEEPALIVE void envoy_on_resolve_dns(uint32_t context_id, uint32_t token,
                                                          uint32_t data_size) {
  getEnvoyRootContext(context_id)->onResolveDns(token, data_size);
}

extern "C" PROXY_WASM_KEEPALIVE void envoy_on_stats_update(uint32_t context_id,
                                                           uint32_t data_size) {
  getEnvoyRootContext(context_id)->onStatsUpdate(data_size);
}
