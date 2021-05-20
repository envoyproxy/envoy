// NOLINT(namespace-envoy)
#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(NetworkTestCpp)

class CloseStreamContext : public Context {
public:
  explicit CloseStreamContext(uint32_t id, RootContext* root) : Context(id, root) {}
  FilterStatus onDownstreamData(size_t data_length, bool end_stream) override;
  FilterStatus onUpstreamData(size_t data_length, bool end_stream) override;
};

class CloseStreamRootContext : public RootContext {
public:
  explicit CloseStreamRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}
};

static RegisterContextFactory register_CloseStreamContext(CONTEXT_FACTORY(CloseStreamContext),
                                                          ROOT_FACTORY(CloseStreamRootContext),
                                                          "close_stream");

FilterStatus CloseStreamContext::onDownstreamData(size_t, bool) {
  closeDownstream();
  return FilterStatus::Continue;
}

FilterStatus CloseStreamContext::onUpstreamData(size_t, bool) {
  closeUpstream();
  return FilterStatus::Continue;
}

END_WASM_PLUGIN
