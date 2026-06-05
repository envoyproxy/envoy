#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "test/extensions/dynamic_modules/stat_sink/test_util.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

// Tracks what the module was asked to do so individual tests can verify the
// wrapper's behavior without needing a separate mock module per assertion.
struct CallRecorder {
  int config_new_calls = 0;
  int config_destroy_calls = 0;
  int flush_calls = 0;
  int histogram_complete_calls = 0;
  std::vector<std::string> histogram_names;
  std::vector<uint64_t> histogram_values;
};

// Globally-addressable recorder so our test-time lambda-style hook
// replacements can reach it. One test at a time.
thread_local CallRecorder* g_recorder = nullptr;

class DynamicModuleStatsSinkTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("stat_sink_no_op", "c"),
        /*do_not_close=*/false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config = newDynamicModuleStatsSinkConfig("test_sink", "test_config",
                                                  std::move(dynamic_module.value()));
    ASSERT_TRUE(config.ok()) << config.status().message();
    config_ = std::move(config.value());
  }

  DynamicModuleStatsSinkConfigSharedPtr config_;
};

TEST_F(DynamicModuleStatsSinkTest, ConfigHasInModuleConfig) {
  // The no_op module returns a non-null config pointer.
  EXPECT_NE(nullptr, config_->in_module_config_);
}

TEST_F(DynamicModuleStatsSinkTest, ConfigHasAllFunctionPointers) {
  EXPECT_NE(nullptr, config_->on_config_destroy_);
  EXPECT_NE(nullptr, config_->on_flush_);
  EXPECT_NE(nullptr, config_->on_histogram_complete_);
}

// Verifies that creating the sink and destroying it triggers the module's
// on_stat_sink_config_destroy exactly once. We substitute the resolved
// function pointers with lambdas that record into a test-local recorder.
TEST_F(DynamicModuleStatsSinkTest, DestructorCallsConfigDestroy) {
  CallRecorder recorder;
  g_recorder = &recorder;

  // Swap in our own hooks for this test so we observe the call. This is safe
  // because the underlying module's own destroy is a no-op.
  config_->on_config_destroy_ = [](envoy_dynamic_module_type_stat_sink_config_module_ptr) {
    g_recorder->config_destroy_calls++;
  };

  {
    DynamicModuleStatsSink sink(config_);
    // No flush or histogram, just verifying the teardown path.
  }

  // Sink destruction releases config_, but config_ is still held by the test
  // fixture (shared_ptr). Destroy the fixture's reference too.
  config_.reset();
  EXPECT_EQ(1, recorder.config_destroy_calls);
  g_recorder = nullptr;
}

// Verify flush() calls the module with a non-null snapshot context.
TEST_F(DynamicModuleStatsSinkTest, FlushCallsModule) {
  CallRecorder recorder;
  g_recorder = &recorder;

  // Swap in a recording hook.
  config_->on_flush_ = [](envoy_dynamic_module_type_stat_sink_config_module_ptr config_ptr,
                          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_ptr) {
    g_recorder->flush_calls++;
    EXPECT_NE(nullptr, config_ptr);
    EXPECT_NE(nullptr, snapshot_ptr);
  };

  DynamicModuleStatsSink sink(config_);
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  // MockMetricSnapshot's default constructor wires every accessor except
  // textReadouts(), so set it up explicitly.
  ON_CALL(snapshot, textReadouts()).WillByDefault(ReturnRef(snapshot.text_readouts_));
  sink.flush(snapshot);

  EXPECT_EQ(1, recorder.flush_calls);
  g_recorder = nullptr;
}

// Flush on an empty snapshot still calls the module with a valid empty context.
TEST_F(DynamicModuleStatsSinkTest, FlushWithEmptySnapshot) {
  CallRecorder recorder;
  g_recorder = &recorder;
  config_->on_flush_ = [](envoy_dynamic_module_type_stat_sink_config_module_ptr,
                          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr) {
    g_recorder->flush_calls++;
  };

  DynamicModuleStatsSink sink(config_);
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  // textReadouts() is not wired in MockMetricSnapshot's default constructor.
  ON_CALL(snapshot, textReadouts()).WillByDefault(ReturnRef(snapshot.text_readouts_));

  sink.flush(snapshot);
  EXPECT_EQ(1, recorder.flush_calls);
  g_recorder = nullptr;
}

// Flush hands the module a snapshot handle wrapping the live snapshot. Verify
// the module can read counters, gauges, and text readouts through it.
TEST_F(DynamicModuleStatsSinkTest, FlushPassesSnapshotContext) {
  CallRecorder recorder;
  g_recorder = &recorder;

  config_->on_flush_ = [](envoy_dynamic_module_type_stat_sink_config_module_ptr,
                          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_ptr) {
    g_recorder->flush_calls++;
    auto* context = static_cast<DynamicModuleStatsSinkFlushContext*>(snapshot_ptr);
    ASSERT_NE(nullptr, context);
    EXPECT_EQ(2u, context->snapshot_.counters().size());
    EXPECT_EQ(1u, context->snapshot_.gauges().size());
    EXPECT_EQ(1u, context->snapshot_.textReadouts().size());
  };

  DynamicModuleStatsSink sink(config_);
  NiceMock<Stats::MockMetricSnapshot> snapshot;

  NiceMock<Stats::MockCounter> c0, c1;
  c0.name_ = "c0";
  c0.value_ = 10;
  c1.name_ = "c1";
  c1.value_ = 20;
  snapshot.counters_.push_back({/*delta=*/5, c0});
  snapshot.counters_.push_back({/*delta=*/0, c1});

  NiceMock<Stats::MockGauge> g0;
  g0.name_ = "g0";
  g0.value_ = 42;
  snapshot.gauges_.push_back(g0);

  ConcreteMockTextReadout t0;
  t0.name_ = "t0";
  t0.value_ = "v0";
  snapshot.text_readouts_.push_back(t0);
  ON_CALL(snapshot, textReadouts()).WillByDefault(ReturnRef(snapshot.text_readouts_));

  sink.flush(snapshot);
  EXPECT_EQ(1, recorder.flush_calls);
  g_recorder = nullptr;
}

// onHistogramComplete must bind the histogram name to a local std::string so the
// buffer stays valid for the module call, since Metric::name() returns by value.
TEST_F(DynamicModuleStatsSinkTest, OnHistogramCompletePassesNameAndValue) {
  CallRecorder recorder;
  g_recorder = &recorder;

  config_->on_histogram_complete_ = [](envoy_dynamic_module_type_stat_sink_config_module_ptr,
                                       envoy_dynamic_module_type_envoy_buffer name,
                                       uint64_t value) {
    g_recorder->histogram_complete_calls++;
    g_recorder->histogram_names.emplace_back(name.ptr, name.length);
    g_recorder->histogram_values.push_back(value);
  };

  DynamicModuleStatsSink sink(config_);
  NiceMock<Stats::MockHistogram> h;
  h.name_ = "my_histogram";

  sink.onHistogramComplete(h, 123);
  sink.onHistogramComplete(h, 456);

  ASSERT_EQ(2, recorder.histogram_complete_calls);
  EXPECT_EQ("my_histogram", recorder.histogram_names[0]);
  EXPECT_EQ("my_histogram", recorder.histogram_names[1]);
  EXPECT_EQ(123u, recorder.histogram_values[0]);
  EXPECT_EQ(456u, recorder.histogram_values[1]);
  g_recorder = nullptr;
}

// When a module is missing its config_new symbol, newDynamicModuleStatsSinkConfig
// must surface a clear error rather than construct a half-built config.
TEST(DynamicModuleStatsSinkConfigTest, FactoryFunctionMissingSymbol) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("stat_sink_missing_config_new", "c"),
      /*do_not_close=*/false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleStatsSinkConfig("test_sink", "test_config",
                                                         std::move(dynamic_module.value()));
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(std::string(config_or_error.status().message()),
              testing::ContainsRegex("config_new"));
}

// When on_stat_sink_config_new returns null, newDynamicModuleStatsSinkConfig
// must surface InvalidArgument rather than wrap a null pointer.
TEST(DynamicModuleStatsSinkConfigTest, FactoryFunctionModuleReturnsNull) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("stat_sink_config_new_fail", "c"),
      /*do_not_close=*/false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config_or_error = newDynamicModuleStatsSinkConfig("test_sink", "test_config",
                                                         std::move(dynamic_module.value()));
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(std::string(config_or_error.status().message()),
              testing::HasSubstr("Failed to initialize dynamic module stats sink config"));
}

} // namespace
} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
