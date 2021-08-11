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

class ResumeCallContext : public Context {
public:
  explicit ResumeCallContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
  FilterDataStatus onRequestBody(size_t, bool) override;
};

class ResumeCallRootContext : public RootContext {
public:
  explicit ResumeCallRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}
};

static RegisterContextFactory register_ResumeCallContext(CONTEXT_FACTORY(ResumeCallContext),
                                                         ROOT_FACTORY(ResumeCallRootContext),
                                                         "resume_call");

FilterHeadersStatus ResumeCallContext::onRequestHeaders(uint32_t, bool) {
  auto context_id = id();
  auto resume_callback = [context_id](uint32_t, size_t, uint32_t) {
    getContext(context_id)->setEffectiveContext();
    logInfo("continueRequest");
    continueRequest();
  };
  if (root()->httpCall("cluster", {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}},
                       "resume", {}, 1000, resume_callback) != WasmResult::Ok) {
    logError("unexpected failure");
    return FilterHeadersStatus::StopIteration;
  }
  logInfo("onRequestHeaders");
  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus ResumeCallContext::onRequestBody(size_t, bool) {
  logInfo("onRequestBody");
  return FilterDataStatus::Continue;
}

END_WASM_PLUGIN
