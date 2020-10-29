#include "envoy/server/lifecycle_notifier.h"

#include "extensions/common/wasm/wasm.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/wasm_base.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class TestContext : public ::Envoy::Extensions::Common::Wasm::Context {
public:
  using ::Envoy::Extensions::Common::Wasm::Context::Context;
  ~TestContext() override = default;
  using ::Envoy::Extensions::Common::Wasm::Context::log;
  proxy_wasm::WasmResult log(uint32_t level, absl::string_view message) override {
    std::cerr << std::string(message) << "\n";
    log_(static_cast<spdlog::level::level_enum>(level), message);
    Extensions::Common::Wasm::Context::log(static_cast<spdlog::level::level_enum>(level), message);
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD2(log_, void(spdlog::level::level_enum level, absl::string_view message));
};

class WasmCommonContextTest
    : public Common::Wasm::WasmTestBase<testing::TestWithParam<std::string>> {
public:
  WasmCommonContextTest() = default;

  void setup(const std::string& code, std::string root_id = "") {
    setupBase(
        GetParam(), code,
        [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
          return new TestContext(wasm, plugin);
        },
        root_id);
  }
  void setupContext() {
    context_ = std::make_unique<TestContext>(wasm_->wasm().get(), root_context_->id(), plugin_);
    context_->onCreate();
  }

  TestContext& rootContext() { return *static_cast<TestContext*>(root_context_); }
  TestContext& context() { return *context_; }

  std::unique_ptr<TestContext> context_;
};

// NB: this is required by VC++ which can not handle the use of macros in the macro definitions
// used by INSTANTIATE_TEST_SUITE_P.
auto testing_values = testing::Values(
#if defined(ENVOY_WASM_V8)
    "v8",
#endif
#if defined(ENVOY_WASM_WAVM)
    "wavm",
#endif
    "null");
INSTANTIATE_TEST_SUITE_P(Runtimes, WasmCommonContextTest, testing_values);

TEST_P(WasmCommonContextTest, OnStat) {
  std::string code;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  if (GetParam() != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(absl::StrCat(
        "{{ test_rundir }}/test/extensions/stats_sinks/wasm/test_data/test_context_cpp.wasm")));
  } else {
    // The name of the Null VM plugin.
    code = "CommonWasmTestContextCpp";
  }
  EXPECT_FALSE(code.empty());
  setup(code);
  setupContext();

  EXPECT_CALL(rootContext(), log_(spdlog::level::warn, Eq("TestRootContext::onStat")));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq("TestRootContext::onStat upstream_rq_2xx:1")));

  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq("TestRootContext::onStat upstream_rq_5xx:2")));

  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq("TestRootContext::onStat membership_total:3")));

  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq("TestRootContext::onStat duration_total:4")));

  EXPECT_CALL(rootContext(), log_(spdlog::level::warn, Eq("TestRootContext::onDone 1")));

  NiceMock<Stats::MockCounter> success_counter;
  success_counter.name_ = "upstream_rq_2xx";
  success_counter.latch_ = 1;
  success_counter.used_ = true;

  NiceMock<Stats::MockCounter> error_5xx_counter;
  error_5xx_counter.name_ = "upstream_rq_5xx";
  error_5xx_counter.latch_ = 1;
  error_5xx_counter.used_ = true;

  snapshot_.counters_.push_back({1, success_counter});
  snapshot_.counters_.push_back({2, error_5xx_counter});

  NiceMock<Stats::MockGauge> membership_total;
  membership_total.name_ = "membership_total";
  membership_total.value_ = 3;
  membership_total.used_ = true;
  snapshot_.gauges_.push_back(membership_total);

  NiceMock<Stats::MockGauge> duration_total;
  duration_total.name_ = "duration_total";
  duration_total.value_ = 4;
  duration_total.used_ = true;
  snapshot_.gauges_.push_back(duration_total);

  rootContext().onStatsUpdate(snapshot_);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
