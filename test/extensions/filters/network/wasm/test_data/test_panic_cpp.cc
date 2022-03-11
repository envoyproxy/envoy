// NOLINT(namespace-envoy)
#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(NetworkTestCpp)

class PanicContext : public Context {
public:
  explicit PanicContext(uint32_t id, RootContext* root) : Context(id, root) {}
  FilterStatus onNewConnection() override;
  FilterStatus onDownstreamData(size_t data_length, bool end_stream) override;
  FilterStatus onUpstreamData(size_t data_length, bool end_stream) override;
};

class PanicRootContext : public RootContext {
public:
  explicit PanicRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}
};

static RegisterContextFactory register_PanicContext(CONTEXT_FACTORY(PanicContext),
                                                    ROOT_FACTORY(PanicRootContext), "panic");

static int* badptr = nullptr;
FilterStatus PanicContext::onNewConnection() {
  *badptr = 0;
  return FilterStatus::Continue;
}

FilterStatus PanicContext::onDownstreamData(size_t, bool) {
  *badptr = 0;
  return FilterStatus::Continue;
}

FilterStatus PanicContext::onUpstreamData(size_t, bool) {
  *badptr = 0;
  return FilterStatus::Continue;
}

END_WASM_PLUGIN
