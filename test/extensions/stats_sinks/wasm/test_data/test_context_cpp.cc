// NOLINT(namespace-envoy)
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

class TestContext : public EnvoyContext {
public:
  explicit TestContext(uint32_t id, RootContext* root) : EnvoyContext(id, root) {}
};

class TestRootContext : public EnvoyRootContext {
public:
  explicit TestRootContext(uint32_t id, std::string_view root_id) : EnvoyRootContext(id, root_id) {}

  void onStatsUpdate(uint32_t result_size) override;
  bool onDone() override;
};

static RegisterContextFactory register_TestContext(CONTEXT_FACTORY(TestContext),
                                                   ROOT_FACTORY(TestRootContext));

void TestRootContext::onStatsUpdate(uint32_t result_size) {
  logWarn("TestRootContext::onStat");
  auto stats_buffer = getBufferBytes(WasmBufferType::CallData, 0, result_size);
  auto stats = parseStatResults(stats_buffer->view());
  for (auto& e : stats.counters) {
    logInfo("TestRootContext::onStat " + std::string(e.name) + ":" + std::to_string(e.delta));
  }
  for (auto& e : stats.gauges) {
    logInfo("TestRootContext::onStat " + std::string(e.name) + ":" + std::to_string(e.value));
  }
}

bool TestRootContext::onDone() {
  logWarn("TestRootContext::onDone " + std::to_string(id()));
  return true;
}

END_WASM_PLUGIN
