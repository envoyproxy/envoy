// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(NetworkTestCpp)

class ResumeCallContext : public Context {
public:
  explicit ResumeCallContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterStatus onDownstreamData(size_t data_length, bool end_stream) override;
  FilterStatus onUpstreamData(size_t data_length, bool end_stream) override;
};

class ResumeCallRootContext : public RootContext {
public:
  explicit ResumeCallRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}
};

static RegisterContextFactory register_ResumeCallContext(CONTEXT_FACTORY(ResumeCallContext),
                                                         ROOT_FACTORY(ResumeCallRootContext),
                                                         "resume_call");

FilterStatus ResumeCallContext::onDownstreamData(size_t, bool) {
  auto context_id = id();
  auto resume_callback = [context_id](uint32_t, size_t, uint32_t) {
    getContext(context_id)->setEffectiveContext();
    if (continueDownstream() == WasmResult::Ok) {
      logInfo("continueDownstream");
    }
  };
  if (root()->httpCall("cluster", {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}},
                       "resume", {}, 1000, resume_callback) != WasmResult::Ok) {
    logError("unexpected failure");
    return FilterStatus::StopIteration;
  }
  logTrace("onDownstreamData " + std::to_string(id()));
  return FilterStatus::StopIteration;
}

FilterStatus ResumeCallContext::onUpstreamData(size_t, bool) {
  auto context_id = id();
  auto resume_callback = [context_id](uint32_t, size_t, uint32_t) {
    getContext(context_id)->setEffectiveContext();
    if (continueUpstream() == WasmResult::Unimplemented) {
      logInfo("continueUpstream unimplemented");
    }
  };
  if (root()->httpCall("cluster", {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}},
                       "resume", {}, 1000, resume_callback) != WasmResult::Ok) {
    logError("unexpected failure");
    return FilterStatus::StopIteration;
  }
  logTrace("onUpstreamData " + std::to_string(id()));
  return FilterStatus::StopIteration;
}

END_WASM_PLUGIN
