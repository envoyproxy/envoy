#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "test/extensions/dynamic_modules/stat_sink/test_util.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {
namespace {

using testing::_;
using testing::Invoke;
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
                                                  std::move(dynamic_module.value()), context_);
    ASSERT_TRUE(config.ok()) << config.status().message();
    config_ = std::move(config.value());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
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
  // The no_op module does not export the optional scheduled hook, so it stays null.
  EXPECT_EQ(nullptr, config_->on_config_scheduled_);
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

// =============================================================================
// Gauge and scheduler config tests
// =============================================================================

// Fixture that builds a config directly so stat creation is not yet frozen, allowing gauge
// definition to be exercised the way a module does during on_stat_sink_config_new.
class DynamicModuleStatsSinkGaugeTest : public testing::Test {
public:
  DynamicModuleStatsSinkConfigSharedPtr makeConfig() {
    return std::make_shared<DynamicModuleStatsSinkConfig>("test_sink", "test_config",
                                                          /*dynamic_module=*/nullptr, context_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

// Gauges defined before freezing get sequential 1-based ids and are created in the scope.
TEST_F(DynamicModuleStatsSinkGaugeTest, DefineGaugeAssignsSequentialIds) {
  auto config = makeConfig();

  size_t id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, config->defineGauge("gauge_a", &id));
  EXPECT_EQ(1u, id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, config->defineGauge("gauge_b", &id));
  EXPECT_EQ(2u, id);

  EXPECT_NE(nullptr, TestUtility::findGauge(context_.store_, "gauge_a"));
  EXPECT_NE(nullptr, TestUtility::findGauge(context_.store_, "gauge_b"));
}

// Defining a gauge after the configuration is created (and stat creation frozen) is rejected.
TEST_F(DynamicModuleStatsSinkGaugeTest, DefineGaugeAfterFrozenIsRejected) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("stat_sink_no_op", "c"),
      /*do_not_close=*/false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();
  auto config = newDynamicModuleStatsSinkConfig("test_sink", "test_config",
                                                std::move(dynamic_module.value()), context_);
  ASSERT_TRUE(config.ok()) << config.status().message();

  size_t id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            config.value()->defineGauge("too_late", &id));
}

// setGauge publishes the value to the underlying scope gauge.
TEST_F(DynamicModuleStatsSinkGaugeTest, SetGaugePublishesValue) {
  auto config = makeConfig();
  size_t id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            config->defineGauge("in_flight", &id));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, config->setGauge(id, 123));
  auto gauge = TestUtility::findGauge(context_.store_, "in_flight");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(123u, gauge->value());
}

// An unknown gauge id (zero or out of range) is rejected.
TEST_F(DynamicModuleStatsSinkGaugeTest, SetGaugeInvalidIdIsRejected) {
  auto config = makeConfig();
  size_t id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success, config->defineGauge("only", &id));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound, config->setGauge(0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound, config->setGauge(2, 1));
}

// onScheduled is a no-op when the module does not implement the scheduled hook.
TEST_F(DynamicModuleStatsSinkGaugeTest, OnScheduledWithoutHookIsNoop) {
  auto config = makeConfig();
  ASSERT_EQ(nullptr, config->on_config_scheduled_);
  config->onScheduled(7); // Must not crash.
}

// onScheduled forwards the Envoy config pointer and the event id to the module hook, which can use
// them to publish a gauge.
TEST_F(DynamicModuleStatsSinkGaugeTest, OnScheduledInvokesHook) {
  auto config = makeConfig();
  size_t id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            config->defineGauge("published", &id));

  // The hook receives the Envoy config pointer and publishes the event id as the gauge value, just
  // like a module aggregating off the main thread would.
  config->on_config_scheduled_ =
      [](envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
         envoy_dynamic_module_type_stat_sink_config_module_ptr, uint64_t event_id) {
        static_cast<DynamicModuleStatsSinkConfig*>(config_envoy_ptr)->setGauge(1, event_id);
      };
  config->onScheduled(55);

  auto gauge = TestUtility::findGauge(context_.store_, "published");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(55u, gauge->value());
}

// commit() locks the config, posts to the main thread dispatcher, and runs the scheduled hook.
TEST_F(DynamicModuleStatsSinkGaugeTest, SchedulerCommitPostsAndRunsHook) {
  auto config = makeConfig();
  size_t id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success, config->defineGauge("g", &id));
  config->on_config_scheduled_ =
      [](envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
         envoy_dynamic_module_type_stat_sink_config_module_ptr, uint64_t event_id) {
        static_cast<DynamicModuleStatsSinkConfig*>(config_envoy_ptr)->setGauge(1, event_id);
      };

  // Run the posted callback inline to model the main thread dispatching the event.
  EXPECT_CALL(context_.dispatcher_, post(_)).WillOnce(Invoke([](Event::PostCb cb) { cb(); }));

  DynamicModuleStatsSinkConfigScheduler scheduler(config->weak_from_this());
  scheduler.commit(42);

  auto gauge = TestUtility::findGauge(context_.store_, "g");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(42u, gauge->value());
}

// commit() on a scheduler whose config has already been destroyed is a safe no-op and never posts.
TEST_F(DynamicModuleStatsSinkGaugeTest, SchedulerCommitOnExpiredConfigIsNoop) {
  std::weak_ptr<DynamicModuleStatsSinkConfig> weak;
  {
    auto config = makeConfig();
    weak = config->weak_from_this();
  }
  ASSERT_TRUE(weak.expired());

  EXPECT_CALL(context_.dispatcher_, post(_)).Times(0);
  DynamicModuleStatsSinkConfigScheduler scheduler(weak);
  scheduler.commit(1); // Must not crash or post.
}

// The callback posted by commit() re-locks the config, so an event that runs after the config has
// been destroyed is dropped instead of touching freed memory.
TEST_F(DynamicModuleStatsSinkGaugeTest, SchedulerPostedEventAfterConfigDestroyedIsDropped) {
  // Set by the hook so the test can confirm it never ran. A lambda with no captures is required
  // to match the C function pointer type, so the flag is reached through a static.
  static bool hook_ran = false;
  hook_ran = false;

  // Capture the posted callback instead of running it inline, simulating an event that arrives
  // after the config has been destroyed.
  Event::PostCb posted_cb;
  EXPECT_CALL(context_.dispatcher_, post(_)).WillOnce(Invoke([&posted_cb](Event::PostCb cb) {
    posted_cb = std::move(cb);
  }));

  auto config = makeConfig();
  config->on_config_scheduled_ = [](envoy_dynamic_module_type_stat_sink_config_envoy_ptr,
                                    envoy_dynamic_module_type_stat_sink_config_module_ptr,
                                    uint64_t) { hook_ran = true; };

  DynamicModuleStatsSinkConfigScheduler scheduler(config->weak_from_this());
  scheduler.commit(99);
  ASSERT_TRUE(posted_cb);

  config.reset();
  posted_cb(); // The re-lock fails, so the hook must be skipped.

  EXPECT_FALSE(hook_ran);
}

// When a module is missing its config_new symbol, newDynamicModuleStatsSinkConfig
// must surface a clear error rather than construct a half-built config.
TEST(DynamicModuleStatsSinkConfigTest, FactoryFunctionMissingSymbol) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("stat_sink_missing_config_new", "c"),
      /*do_not_close=*/false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto config_or_error = newDynamicModuleStatsSinkConfig(
      "test_sink", "test_config", std::move(dynamic_module.value()), context);
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

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto config_or_error = newDynamicModuleStatsSinkConfig(
      "test_sink", "test_config", std::move(dynamic_module.value()), context);
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(std::string(config_or_error.status().message()),
              testing::HasSubstr("Failed to initialize dynamic module stats sink config"));
}

} // namespace
} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
