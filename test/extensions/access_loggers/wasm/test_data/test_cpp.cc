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

  void onLog() override;

private:
  int32_t on_log_count_ = 0;
};
static RegisterContextFactory register_ExampleContext(ROOT_FACTORY(TestRootContext));

void TestRootContext::onLog() {
  // If on_log is called twice, cause panic.
  if (++on_log_count_ == 2) {
    static int32_t* bad_ptr = nullptr;
    *bad_ptr = 1;
  }

  auto path = getRequestHeader(":path");
  logWarn("onLog " + std::to_string(id()) + " " + std::string(path->view()));
}

END_WASM_PLUGIN
