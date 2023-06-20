// NOLINT(namespace-envoy)
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(AccessLoggerTestCpp)

class TestRootContext : public RootContext {
public:
  using RootContext::RootContext;
  bool onConfigure(size_t) override;

  void onLog() override;
};
static RegisterContextFactory register_ExampleContext(ROOT_FACTORY(TestRootContext));

bool TestRootContext::onConfigure(size_t size) {
  if (size > 0 &&
      getBufferBytes(WasmBufferType::PluginConfiguration, 0, size)->toString() == "invalid") {
    return false;
  }
  return true;
}

void TestRootContext::onLog() {
  auto path = getRequestHeader(":path");
  logWarn("onLog " + std::to_string(id()) + " " + std::string(path->view()));
}

END_WASM_PLUGIN
