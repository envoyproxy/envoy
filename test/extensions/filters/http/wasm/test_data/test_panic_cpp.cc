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

class PanicRootContext : public RootContext {
public:
  explicit PanicRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}
};

class PanicContext : public Context {
public:
  explicit PanicContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
  FilterDataStatus onRequestBody(size_t , bool ) override;
  FilterTrailersStatus onRequestTrailers(uint32_t) override;
  FilterHeadersStatus onResponseHeaders(uint32_t, bool) override;
  FilterDataStatus onResponseBody(size_t, bool) override;
  FilterTrailersStatus onResponseTrailers(uint32_t) override;
};

static RegisterContextFactory register_PanicContext(CONTEXT_FACTORY(PanicContext),
                                                   ROOT_FACTORY(PanicRootContext), "panic");

static int* badptr = nullptr;

FilterHeadersStatus PanicContext::onRequestHeaders(uint32_t, bool) {
  *badptr = 0;
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus PanicContext::onResponseHeaders(uint32_t, bool) {
  *badptr = 0;
  return FilterHeadersStatus::Continue;
}

FilterTrailersStatus PanicContext::onRequestTrailers(uint32_t) {
  *badptr = 0;
  return FilterTrailersStatus::Continue;
}

FilterDataStatus PanicContext::onRequestBody(size_t , bool ) {
  *badptr = 0;
  return FilterDataStatus::Continue;
}

FilterDataStatus PanicContext::onResponseBody(size_t, bool) {
  *badptr = 0;
  return FilterDataStatus::Continue;
}

FilterTrailersStatus PanicContext::onResponseTrailers(uint32_t) {
  *badptr = 0;
  return FilterTrailersStatus::Continue;
}

END_WASM_PLUGIN
