// NOLINT(namespace-envoy)
#include <climits>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#include "source/extensions/common/wasm/ext/envoy_proxy_wasm_api.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(CommonWasmTestContextCpp)

class TestRootContext : public EnvoyRootContext {
public:
  explicit TestRootContext(uint32_t id, std::string_view root_id) : EnvoyRootContext(id, root_id) {}

  bool onStart(size_t vm_configuration_size) override;
  bool onDone() override;
  void onTick() override;
  void onQueueReady(uint32_t) override;
  void onResolveDns(uint32_t token, uint32_t results_size) override;

  std::string test_;

private:
  uint32_t dns_token_;
};

class TestContext : public EnvoyContext {
public:
  explicit TestContext(uint32_t id, RootContext* root) : EnvoyContext(id, root) {}
  FilterDataStatus onRequestBody(size_t body_buffer_length, bool end_of_stream) override;
  FilterHeadersStatus onResponseHeaders(uint32_t, bool) override;

private:
  TestRootContext* root() { return static_cast<TestRootContext*>(Context::root()); }
};

static RegisterContextFactory register_TestContext(CONTEXT_FACTORY(TestContext),
                                                   ROOT_FACTORY(TestRootContext));
static RegisterContextFactory register_EmptyTestContext(CONTEXT_FACTORY(EnvoyContext),
                                                        ROOT_FACTORY(EnvoyRootContext), "empty");

FilterDataStatus TestContext::onRequestBody(size_t, bool) {
  auto test = root()->test_;
  if (test == "duplicate_local_reply") {
    sendLocalResponse(200, "ok", "body", {});
    sendLocalResponse(200, "fail", "body", {});
  }
  return FilterDataStatus::Continue;
}

FilterHeadersStatus TestContext::onResponseHeaders(uint32_t, bool) {
  return FilterHeadersStatus::Continue;
}

bool TestRootContext::onStart(size_t configuration_size) {
  test_ = getBufferBytes(WasmBufferType::VmConfiguration, 0, configuration_size)->toString();
  if (test_ == "dns_resolve") {
    envoy_resolve_dns("example.com", sizeof("example.com") - 1, &dns_token_);
  }
  return true;
}

void TestRootContext::onResolveDns(uint32_t token, uint32_t result_size) {
  logWarn("TestRootContext::onResolveDns " + std::to_string(token));
  auto dns_buffer = getBufferBytes(WasmBufferType::CallData, 0, result_size);
  auto dns = parseDnsResults(dns_buffer->view());
  for (auto& e : dns) {
    logInfo("TestRootContext::onResolveDns dns " + std::to_string(e.ttl_seconds) + " " + e.address);
  }
}

bool TestRootContext::onDone() {
  logWarn("TestRootContext::onDone " + std::to_string(id()));
  return true;
}

// Null VM fails on nullptr.
void TestRootContext::onTick() {
  if (test_ == "dns_resolve") {
    if (envoy_resolve_dns(nullptr, 1, &dns_token_) != WasmResult::InvalidMemoryAccess) {
      logInfo("resolve_dns should report invalid memory access");
    }
    if (envoy_resolve_dns("example.com", sizeof("example.com") - 1, nullptr) !=
        WasmResult::InvalidMemoryAccess) {
      logInfo("resolve_dns should report invalid memory access");
    }
  }
}

// V8 fails on pointer too large.
void TestRootContext::onQueueReady(uint32_t) {
  if (test_ == "dns_resolve") {
    if (envoy_resolve_dns(reinterpret_cast<char*>(INT_MAX), 0, &dns_token_) !=
        WasmResult::InvalidMemoryAccess) {
      logInfo("resolve_dns should report invalid memory access");
    }
    if (envoy_resolve_dns("example.com", sizeof("example.com") - 1,
                          reinterpret_cast<uint32_t*>(INT_MAX)) !=
        WasmResult::InvalidMemoryAccess) {
      logInfo("resolve_dns should report invalid memory access");
    }
  }
}

END_WASM_PLUGIN
