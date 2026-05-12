#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/wasm_base.h"

#include "contrib/stat_sinks/wasm_filter/source/wasm_filter_stat_sink_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {

class TestContext : public ::Envoy::Extensions::Common::Wasm::Context {
public:
  using ::Envoy::Extensions::Common::Wasm::Context::Context;
  ~TestContext() override = default;
  using ::Envoy::Extensions::Common::Wasm::Context::log;
  proxy_wasm::WasmResult log(uint32_t level, std::string_view message) override {
    std::cerr << message << "\n";
    log_(static_cast<spdlog::level::level_enum>(level), toAbslStringView(message));
    Extensions::Common::Wasm::Context::log(static_cast<spdlog::level::level_enum>(level), message);
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD(void, log_, (spdlog::level::level_enum level, absl::string_view message));
};

class WasmFilterStatSinkIntegrationTest
    : public Common::Wasm::WasmTestBase<
          testing::TestWithParam<std::tuple<std::string, std::string>>> {
public:
  WasmFilterStatSinkIntegrationTest() = default;

  void setup(const std::string& code, std::string root_id = "") {
    setRootId(root_id);
    setupBase(std::get<0>(GetParam()), code,
              [](Common::Wasm::Wasm* wasm, const std::shared_ptr<Common::Wasm::Plugin>& plugin)
                  -> proxy_wasm::ContextBase* { return new NiceMock<TestContext>(wasm, plugin); });
  }

  TestContext& rootContext() { return *static_cast<TestContext*>(root_context_); }

  void setupSnapshot() {
    kept_counter_.name_ = "upstream_rq_2xx";
    kept_counter_.latch_ = 10;
    kept_counter_.used_ = true;

    excluded_counter_.name_ = "excluded_debug_stat";
    excluded_counter_.latch_ = 5;
    excluded_counter_.used_ = true;

    gauge_.name_ = "membership_total";
    gauge_.value_ = 42;
    gauge_.used_ = true;

    snapshot_.counters_ = {{10, kept_counter_}, {5, excluded_counter_}};
    snapshot_.gauges_ = {gauge_};

    ON_CALL(snapshot_, counters()).WillByDefault(testing::ReturnRef(snapshot_.counters_));
    ON_CALL(snapshot_, gauges()).WillByDefault(testing::ReturnRef(snapshot_.gauges_));
    ON_CALL(snapshot_, histograms()).WillByDefault(testing::ReturnRef(snapshot_.histograms_));
    ON_CALL(snapshot_, textReadouts()).WillByDefault(testing::ReturnRef(snapshot_.text_readouts_));
    ON_CALL(snapshot_, hostCounters()).WillByDefault(testing::ReturnRef(snapshot_.host_counters_));
    ON_CALL(snapshot_, hostGauges()).WillByDefault(testing::ReturnRef(snapshot_.host_gauges_));
  }

  NiceMock<Stats::MockCounter> kept_counter_;
  NiceMock<Stats::MockCounter> excluded_counter_;
  NiceMock<Stats::MockGauge> gauge_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmFilterStatSinkIntegrationTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmFilterStatSinkIntegrationTest, PluginSetsGlobalTagsAndFiltersMetrics) {
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir "
                     "}}/contrib/stat_sinks/wasm_filter/test/test_data/stats_filter_plugin.wasm")));
  } else {
    code = "StatsFilterTestPlugin";
  }
  EXPECT_FALSE(code.empty());
  setup(code);
  setupSnapshot();

  EXPECT_CALL(rootContext(), log_(spdlog::level::info,
                                  Eq("StatsFilterTestPlugin: onStatsUpdate counters=2 gauges=1")));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq("StatsFilterTestPlugin: kept counters=1 histograms=0")));
  EXPECT_CALL(rootContext(), log_(spdlog::level::info, Eq("StatsFilterTestPlugin: emit complete")));

  rootContext().onStatsUpdate(snapshot_);
}

} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
