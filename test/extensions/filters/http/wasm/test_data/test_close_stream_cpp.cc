// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_lite.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(HttpWasmTestCpp)

class CloseStreamRootContext : public RootContext {
public:
  explicit CloseStreamRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}
};

class CloseStreamContext : public Context {
public:
  explicit CloseStreamContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
  FilterHeadersStatus onResponseHeaders(uint32_t, bool) override;
};

static RegisterContextFactory register_CloseStreamContext(CONTEXT_FACTORY(CloseStreamContext),
                                                   ROOT_FACTORY(CloseStreamRootContext), "close_stream");

FilterHeadersStatus CloseStreamContext::onRequestHeaders(uint32_t, bool) {
  closeRequest();
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus CloseStreamContext::onResponseHeaders(uint32_t, bool) {
  closeResponse();
  return FilterHeadersStatus::Continue;
}

END_WASM_PLUGIN
