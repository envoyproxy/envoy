// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_lite.h"
#else
#include "extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(HttpWasmTestCpp)

class AsyncCallContext : public Context {
public:
  explicit AsyncCallContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

class AsyncCallRootContext : public RootContext {
public:
  explicit AsyncCallRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}
};

static RegisterContextFactory register_AsyncCallContext(CONTEXT_FACTORY(AsyncCallContext),
                                                        ROOT_FACTORY(AsyncCallRootContext),
                                                        "async_call");

FilterHeadersStatus AsyncCallContext::onRequestHeaders(uint32_t, bool end_of_stream) {
  auto context_id = id();
  auto callback = [context_id](uint32_t, size_t body_size, uint32_t) {
    if (body_size == 0) {
      logInfo("async_call failed");
      return;
    }
    auto response_headers = getHeaderMapPairs(WasmHeaderMapType::HttpCallResponseHeaders);
    // Switch context after getting headers, but before getting body to exercise both code paths.
    getContext(context_id)->setEffectiveContext();
    auto body = getBufferBytes(WasmBufferType::HttpCallResponseBody, 0, body_size);
    auto response_trailers = getHeaderMapPairs(WasmHeaderMapType::HttpCallResponseTrailers);
    for (auto& p : response_headers->pairs()) {
      logInfo(std::string(p.first) + std::string(" -> ") + std::string(p.second));
    }
    logDebug(std::string(body->view()));
    for (auto& p : response_trailers->pairs()) {
      logWarn(std::string(p.first) + std::string(" -> ") + std::string(p.second));
    }
  };
  if (end_of_stream) {
    if (root()->httpCall("cluster", {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}},
                         "hello world", {{"trail", "cow"}}, 1000, callback) == WasmResult::Ok) {
      logError("expected failure did not");
    }
    return FilterHeadersStatus::Continue;
  }
  if (root()->httpCall("bogus cluster",
                       {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}}, "hello world",
                       {{"trail", "cow"}}, 1000, callback) == WasmResult::Ok) {
    logError("bogus cluster found error");
  }
  if (root()->httpCall("cluster", {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}},
                       "hello world", {{"trail", "cow"}}, 0xFFFFFFFF, callback) == WasmResult::Ok) {
    logError("bogus timeout accepted error");
  }
  if (root()->httpCall("cluster", {{":method", "POST"}, {":authority", "foo"}}, "hello world",
                       {{"trail", "cow"}}, 1000, callback) == WasmResult::Ok) {
    logError("emissing path accepted error");
  }
  root()->httpCall("cluster", {{":method", "POST"}, {":path", "/"}, {":authority", "foo"}},
                   "hello world", {{"trail", "cow"}}, 1000, callback);
  logInfo("onRequestHeaders");
  return FilterHeadersStatus::StopIteration;
}

END_WASM_PLUGIN
