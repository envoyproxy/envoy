#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest-param-test.h"
#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace Wasm {

class TestContext : public Extensions::Common::Wasm::Context {
public:
  TestContext(Extensions::Common::Wasm::Wasm* wasm,
              const std::shared_ptr<Extensions::Common::Wasm::Plugin>& plugin)
      : Extensions::Common::Wasm::Context(wasm, plugin) {}
  ~TestContext() override = default;
  using Extensions::Common::Wasm::Context::log;
  proxy_wasm::WasmResult log(uint32_t level, std::string_view message) override {
    std::cerr << message << "\n";
    log_(static_cast<spdlog::level::level_enum>(level), toAbslStringView(message));
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD(void, log_, (spdlog::level::level_enum level, absl::string_view message));
};

class WasmTestBase {
public:
  WasmTestBase()
      : api_(Api::createApiForTest(stats_store_)),
        dispatcher_(api_->allocateDispatcher("wasm_test")),
        base_scope_(stats_store_.createScope("")), scope_(base_scope_->createScope("")) {}

  void createWasm(absl::string_view runtime) {
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    *plugin_config.mutable_name() = name_;
    *plugin_config.mutable_root_id() = root_id_;
    *plugin_config.mutable_vm_config()->mutable_runtime() =
        absl::StrCat("envoy.wasm.runtime.", runtime);
    *plugin_config.mutable_vm_config()->mutable_vm_id() = vm_id_;
    plugin_config.mutable_vm_config()->mutable_configuration()->set_value(vm_configuration_);
    plugin_config.mutable_configuration()->set_value(plugin_configuration_);
    plugin_ = std::make_shared<Extensions::Common::Wasm::Plugin>(
        plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info_,
        nullptr);
    auto config = plugin_->wasmConfig();
    config.allowedCapabilities() = allowed_capabilities_;
    config.environmentVariables() = envs_;
    wasm_ = std::make_shared<Extensions::Common::Wasm::Wasm>(config, vm_key_, scope_, *api_,
                                                             cluster_manager, *dispatcher_);
    EXPECT_NE(wasm_, nullptr);
    wasm_->setCreateContextForTesting(
        nullptr,
        [](Extensions::Common::Wasm::Wasm* wasm,
           const std::shared_ptr<Extensions::Common::Wasm::Plugin>& plugin)
            -> proxy_wasm::ContextBase* { return new TestContext(wasm, plugin); });
  }

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher_;
  Stats::ScopeSharedPtr base_scope_;
  Stats::ScopeSharedPtr scope_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::string name_;
  std::string root_id_;
  std::string vm_id_;
  std::string vm_configuration_;
  std::string vm_key_;
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities_;
  Extensions::Common::Wasm::EnvironmentVariableMap envs_{};
  std::string plugin_configuration_;
  std::shared_ptr<Extensions::Common::Wasm::Plugin> plugin_;
  std::shared_ptr<Extensions::Common::Wasm::Wasm> wasm_;
};

class WasmTest : public WasmTestBase,
                 public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  void createWasm() { WasmTestBase::createWasm(std::get<0>(GetParam())); }
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmTest,
                         Envoy::Extensions::Common::Wasm::sandbox_runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WasmTest);

class WasmNullTest : public WasmTestBase,
                     public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  void createWasm() {
    WasmTestBase::createWasm(std::get<0>(GetParam()));
    const auto code =
        std::get<0>(GetParam()) != "null"
            ? TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
                  "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/stats_cpp.wasm"))
            : "WasmStatsCpp";
    EXPECT_FALSE(code.empty());
    EXPECT_TRUE(wasm_->load(code, false));
    EXPECT_TRUE(wasm_->initialize());
  }
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmNullTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values);

class WasmTestMatrix : public WasmTestBase,
                       public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  void createWasm() { WasmTestBase::createWasm(std::get<0>(GetParam())); }

  void setWasmCode(std::string vm_configuration) {
    const auto basic_path =
        absl::StrCat("test/extensions/bootstrap/wasm/test_data/", vm_configuration);
    code_ = TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath(basic_path + "_" + std::get<1>(GetParam()) + ".wasm"));

    EXPECT_FALSE(code_.empty());
  }

protected:
  std::string code_;
};

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmTestMatrix,
                         Envoy::Extensions::Common::Wasm::sandbox_runtime_and_language_values);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WasmTestMatrix);

TEST_P(WasmTestMatrix, LoggingWithEnvVars) {
  plugin_configuration_ = "configure-test";
  envs_ = {{"ON_TICK", "TICK_VALUE"}, {"ON_CONFIGURE", "CONFIGURE_VALUE"}};
  createWasm();
  setWasmCode("logging");
  auto wasm_weak = std::weak_ptr<Extensions::Common::Wasm::Wasm>(wasm_);
  auto wasm_handler = std::make_unique<Extensions::Common::Wasm::WasmHandle>(std::move(wasm_));

  EXPECT_TRUE(wasm_weak.lock()->load(code_, false));
  EXPECT_TRUE(wasm_weak.lock()->initialize());
  auto context = static_cast<TestContext*>(wasm_weak.lock()->start(plugin_));
  if (std::get<1>(GetParam()) == "cpp") {
    EXPECT_CALL(*context, log_(spdlog::level::info, Eq("printf stdout test")));
    EXPECT_CALL(*context, log_(spdlog::level::err, Eq("printf stderr test")));
  }
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("warn configure-test")));
  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("test trace logging")));
  EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("test debug logging")));
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("test error logging")));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("test tick logging")))
      .Times(testing::AtLeast(1));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("onDone logging")));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("onDelete logging")));
  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("ON_CONFIGURE: CONFIGURE_VALUE")));
  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("ON_TICK: TICK_VALUE")));

  EXPECT_TRUE(wasm_weak.lock()->configure(context, plugin_));
  wasm_handler.reset();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // This will `SEGV` on nullptr if wasm has been deleted.
  context->onTick(0);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher_->clearDeferredDeleteList();
}

TEST_P(WasmTest, BadSignature) {
  createWasm();
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/bad_signature_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm_->load(code, false));
  EXPECT_FALSE(wasm_->initialize());
  EXPECT_TRUE(wasm_->isFailed());
}

TEST_P(WasmTest, Segv) {
  createWasm();
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/segv_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm_->load(code, false));
  EXPECT_TRUE(wasm_->initialize());
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("before badptr")));
  EXPECT_FALSE(wasm_->configure(context, plugin_));
  EXPECT_TRUE(wasm_->isFailed());
}

TEST_P(WasmTest, DivByZero) {
  createWasm();
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/segv_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm_->load(code, false));
  EXPECT_TRUE(wasm_->initialize());
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("before div by zero")));
  context->onLog();
  EXPECT_TRUE(wasm_->isFailed());
}

TEST_P(WasmTest, IntrinsicGlobals) {
  createWasm();
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/emscripten_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm_->load(code, false));
  EXPECT_TRUE(wasm_->initialize());
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("NaN nan")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("inf inf"))).Times(3);
  EXPECT_TRUE(wasm_->configure(context, plugin_));
}

// The `asm2wasm.wasm` file uses operations which would require the `asm2wasm` Emscripten module
// *if* em++ is invoked with the trap mode "clamp". See
// https://emscripten.org/docs/compiling/WebAssembly.html This test demonstrates that the `asm2wasm`
// module is not required with the trap mode is set to "allow". Note: future Wasm standards will
// change this behavior by providing non-trapping instructions, but in the mean time we support the
// default Emscripten behavior.
TEST_P(WasmTest, Asm2Wasm) {
  createWasm();
  const auto code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/asm2wasm_cpp.wasm"));
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm_->load(code, false));
  EXPECT_TRUE(wasm_->initialize());
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("out 0 0 0")));
  EXPECT_TRUE(wasm_->configure(context, plugin_));
}

TEST_P(WasmNullTest, Stats) {
  createWasm();
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));

  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("get counter = 1")));
  EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("get counter = 2")));
  // recordMetric on a Counter is the same as increment.
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("get counter = 5")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("get gauge = 2")));
  // Get is not supported on histograms.
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("get histogram = Unsupported")));

  EXPECT_TRUE(wasm_->configure(context, plugin_));
  EXPECT_EQ(scope_->counterFromString("wasmcustom.test_counter").value(), 5);
  EXPECT_EQ(scope_->gaugeFromString("wasmcustom.test_gauge", Stats::Gauge::ImportMode::Accumulate)
                .value(),
            2);
}

TEST_P(WasmNullTest, StatsHigherLevel) {
  createWasm();
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));

  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("get counter = 1")));
  EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("get counter = 2")));
  // recordMetric on a Counter is the same as increment.
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("get counter = 5")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("get gauge = 2")));
  // Get is not supported on histograms.
  EXPECT_CALL(*context, log_(spdlog::level::err,
                             Eq(std::string("resolved histogram name = "
                                            "histogram_int_tag.7.histogram_string_tag.test_tag."
                                            "histogram_bool_tag.true.test_histogram"))));

  wasm_->setTimerPeriod(1, std::chrono::milliseconds(10));
  wasm_->tickHandler(1);
  EXPECT_EQ(scope_->counterFromString("wasmcustom.counter_tag.test_tag.test_counter").value(), 5);
  EXPECT_EQ(scope_
                ->gaugeFromString("wasmcustom.gauge_int_tag.9.test_gauge",
                                  Stats::Gauge::ImportMode::Accumulate)
                .value(),
            2);
}

TEST_P(WasmNullTest, StatsHighLevel) {
  createWasm();
  auto context = static_cast<TestContext*>(wasm_->start(plugin_));

  EXPECT_CALL(*context, log_(spdlog::level::trace, Eq("get counter = 1")));
  EXPECT_CALL(*context, log_(spdlog::level::debug, Eq("get counter = 2")));
  // recordMetric on a Counter is the same as increment.
  EXPECT_CALL(*context, log_(spdlog::level::info, Eq("get counter = 5")));
  EXPECT_CALL(*context, log_(spdlog::level::warn, Eq("get gauge = 2")));
  // Get is not supported on histograms.
  // EXPECT_CALL(*context, log_(spdlog::level::err, Eq(std::string("resolved histogram name
  // = int_tag.7_string_tag.test_tag.bool_tag.true.test_histogram"))));
  EXPECT_CALL(*context,
              log_(spdlog::level::err,
                   Eq("h_id = int_tag.7.string_tag.test_tag.bool_tag.true.test_histogram")));
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("stack_c = 1")));
  EXPECT_CALL(*context, log_(spdlog::level::err, Eq("stack_g = 2")));
  // Get is not supported on histograms.
  // EXPECT_CALL(*context, log_(spdlog::level::err, Eq("stack_h = 3")));
  context->onLog();
  EXPECT_EQ(
      scope_
          ->counterFromString("wasmcustom.string_tag.test_tag.int_tag.7.bool_tag.true.test_counter")
          .value(),
      5);
  EXPECT_EQ(
      scope_
          ->gaugeFromString("wasmcustom.string_tag1.test_tag1.string_tag2.test_tag2.test_gauge",
                            Stats::Gauge::ImportMode::Accumulate)
          .value(),
      2);
}

} // namespace Wasm
} // namespace Extensions
} // namespace Envoy
