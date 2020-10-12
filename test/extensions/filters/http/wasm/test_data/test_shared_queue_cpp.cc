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

class SharedQueueContext : public Context {
public:
  explicit SharedQueueContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

class SharedQueueRootContext : public RootContext {
public:
  explicit SharedQueueRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}

  bool onStart(size_t) override;
  void onQueueReady(uint32_t) override;

  uint32_t shared_queue_token_;
};

static RegisterContextFactory register_SharedQueueContext(CONTEXT_FACTORY(SharedQueueContext),
                                                          ROOT_FACTORY(SharedQueueRootContext),
                                                          "shared_queue");

bool SharedQueueRootContext::onStart(size_t) {
  CHECK_RESULT(registerSharedQueue("my_shared_queue", &shared_queue_token_));
  return true;
}

FilterHeadersStatus SharedQueueContext::onRequestHeaders(uint32_t, bool) {
  uint32_t token;
  if (resolveSharedQueue("vm_id", "bad_shared_queue", &token) == WasmResult::NotFound) {
    logWarn("onRequestHeaders not found bad_shared_queue");
  }
  CHECK_RESULT(resolveSharedQueue("vm_id", "my_shared_queue", &token));
  if (enqueueSharedQueue(token, "data1") == WasmResult::Ok) {
    logWarn("onRequestHeaders enqueue Ok");
  }
  return FilterHeadersStatus::Continue;
}

void SharedQueueRootContext::onQueueReady(uint32_t token) {
  if (token == shared_queue_token_) {
    logInfo("onQueueReady");
  }
  std::unique_ptr<WasmData> data;
  if (dequeueSharedQueue(9999999 /* bad token */, &data) == WasmResult::NotFound) {
    logWarn("onQueueReady bad token not found");
  }
  if (dequeueSharedQueue(token, &data) == WasmResult::Ok) {
    logDebug("data " + data->toString() + " Ok");
  }
  if (dequeueSharedQueue(token, &data) == WasmResult::Empty) {
    logWarn("onQueueReady extra data not found");
  }
}

END_WASM_PLUGIN
