#include <chrono>
#include <iostream>

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/gradient_controller.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::Ge;
using testing::Le;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {
namespace {

GradientControllerConfig makeConfig(const std::string& yaml_config,
                                    NiceMock<Runtime::MockLoader>& runtime) {
  envoy::extensions::filters::http::adaptive_concurrency::v3::GradientControllerConfig proto;
  TestUtility::loadFromYamlAndValidate(yaml_config, proto);
  return GradientControllerConfig{proto, runtime};
}

class GradientControllerConfigTest : public testing::Test {
public:
  GradientControllerConfigTest() = default;

protected:
  NiceMock<Runtime::MockLoader> runtime_;
};

class GradientControllerTest : public testing::Test {
public:
  GradientControllerTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher()) {}

  GradientControllerSharedPtr makeController(const std::string& yaml_config) {
    return std::make_shared<GradientController>(makeConfig(yaml_config, runtime_), *dispatcher_,
                                                runtime_, "test_prefix.", stats_, random_);
  }

protected:
  // Helper function that will attempt to pull forwarding decisions.
  void tryForward(const GradientControllerSharedPtr& controller,
                  const bool expect_forward_response) {
    const auto expected_resp =
        expect_forward_response ? RequestForwardingAction::Forward : RequestForwardingAction::Block;
    EXPECT_EQ(expected_resp, controller->forwardingDecision());
  }

  // Gets the controller past the initial minRTT stage.
  void advancePastMinRTTStage(const GradientControllerSharedPtr& controller,
                              const std::string& yaml_config,
                              std::chrono::milliseconds latency = std::chrono::milliseconds(5)) {
    const auto config = makeConfig(yaml_config, runtime_);
    for (uint32_t i = 0; i <= config.minRTTAggregateRequestCount(); ++i) {
      tryForward(controller, true);
      controller->recordLatencySample(latency);
    }
  }

  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(GradientControllerConfigTest, BasicTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 42.5
concurrency_limit_params:
  max_concurrency_limit: 1337
  concurrency_update_interval: 0.123s
min_rtt_calc_params:
  jitter:
    value: 13.2
  interval: 31s
  request_count: 52
  min_concurrency: 8
)EOF";

  auto config = makeConfig(yaml, runtime_);

  EXPECT_EQ(config.minRTTCalcInterval(), std::chrono::seconds(31));
  EXPECT_EQ(config.sampleRTTCalcInterval(), std::chrono::milliseconds(123));
  EXPECT_EQ(config.maxConcurrencyLimit(), 1337);
  EXPECT_EQ(config.minRTTAggregateRequestCount(), 52);
  EXPECT_EQ(config.sampleAggregatePercentile(), .425);
  EXPECT_EQ(config.jitterPercent(), .132);
  EXPECT_EQ(config.minConcurrency(), 8);
}

TEST_F(GradientControllerConfigTest, Clamping) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 42.5
concurrency_limit_params:
  max_concurrency_limit: 1337
  concurrency_update_interval:
    nanos: 123000000
min_rtt_calc_params:
  jitter:
    value: 13.2
  interval:
    seconds: 31
  request_count: 52
)EOF";

  auto config = makeConfig(yaml, runtime_);

  // Should be clamped in the range [0,1].

  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 42.5)).WillOnce(Return(150.0));
  EXPECT_EQ(config.sampleAggregatePercentile(), 1.0);
  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 42.5)).WillOnce(Return(-50.5));
  EXPECT_EQ(config.sampleAggregatePercentile(), 0.0);

  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 13.2)).WillOnce(Return(150.0));
  EXPECT_EQ(config.jitterPercent(), 1.0);
  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 13.2)).WillOnce(Return(-50.5));
  EXPECT_EQ(config.jitterPercent(), 0.0);
}

TEST_F(GradientControllerConfigTest, BasicTestOverrides) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 42.5
concurrency_limit_params:
  max_concurrency_limit: 1337
  concurrency_update_interval:
    nanos: 123000000
min_rtt_calc_params:
  buffer:
    value: 33
  jitter:
    value: 13.2
  interval:
    seconds: 31
  request_count: 52
  min_concurrency: 7
)EOF";

  auto config = makeConfig(yaml, runtime_);

  EXPECT_CALL(runtime_.snapshot_, getInteger(_, 31000)).WillOnce(Return(60000));
  EXPECT_EQ(config.minRTTCalcInterval(), std::chrono::seconds(60));

  EXPECT_CALL(runtime_.snapshot_, getInteger(_, 123)).WillOnce(Return(456));
  EXPECT_EQ(config.sampleRTTCalcInterval(), std::chrono::milliseconds(456));

  EXPECT_CALL(runtime_.snapshot_, getInteger(_, 1337)).WillOnce(Return(9000));
  EXPECT_EQ(config.maxConcurrencyLimit(), 9000);

  EXPECT_CALL(runtime_.snapshot_, getInteger(_, 52)).WillOnce(Return(65));
  EXPECT_EQ(config.minRTTAggregateRequestCount(), 65);

  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 42.5)).WillOnce(Return(66.0));
  EXPECT_EQ(config.sampleAggregatePercentile(), .66);

  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 13.2)).WillOnce(Return(15.5));
  EXPECT_EQ(config.jitterPercent(), .155);

  EXPECT_CALL(runtime_.snapshot_, getInteger(_, 7)).WillOnce(Return(9));
  EXPECT_EQ(config.minConcurrency(), 9);

  EXPECT_CALL(runtime_.snapshot_, getDouble(_, 33.0)).WillOnce(Return(77.0));
  EXPECT_EQ(config.minRTTBufferPercent(), .77);
}

TEST_F(GradientControllerConfigTest, DefaultValuesTest) {
  const std::string yaml = R"EOF(
concurrency_limit_params:
  concurrency_update_interval: 0.123s
min_rtt_calc_params:
  interval: 31s
)EOF";

  auto config = makeConfig(yaml, runtime_);

  EXPECT_EQ(config.minRTTCalcInterval(), std::chrono::seconds(31));
  EXPECT_EQ(config.sampleRTTCalcInterval(), std::chrono::milliseconds(123));
  EXPECT_EQ(config.maxConcurrencyLimit(), 1000);
  EXPECT_EQ(config.minRTTAggregateRequestCount(), 50);
  EXPECT_EQ(config.sampleAggregatePercentile(), .5);
  EXPECT_EQ(config.jitterPercent(), .15);
  EXPECT_EQ(config.minConcurrency(), 3);
  EXPECT_EQ(config.minRTTBufferPercent(), 0.25);
}

TEST_F(GradientControllerTest, MinRTTLogicTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 50
  min_concurrency: 7
)EOF";

  auto controller = makeController(yaml);
  const auto min_rtt = std::chrono::milliseconds(13);

  // The controller should be measuring minRTT upon creation, so the concurrency window is 7 (the
  // min concurrency).
  EXPECT_EQ(
      1,
      stats_.gauge("test_prefix.min_rtt_calculation_active", Stats::Gauge::ImportMode::Accumulate)
          .value());
  EXPECT_EQ(controller->concurrencyLimit(), 7);
  for (int i = 0; i < 7; ++i) {
    tryForward(controller, true);
  }
  tryForward(controller, false);
  tryForward(controller, false);
  for (int i = 0; i < 7; ++i) {
    controller->recordLatencySample(min_rtt);
  }

  // 43 more requests should cause the minRTT to be done calculating.
  for (int i = 0; i < 43; ++i) {
    EXPECT_EQ(controller->concurrencyLimit(), 7);
    tryForward(controller, true);
    controller->recordLatencySample(min_rtt);
  }

  // Verify the minRTT value measured is accurate.
  EXPECT_EQ(
      0,
      stats_.gauge("test_prefix.min_rtt_calculation_active", Stats::Gauge::ImportMode::Accumulate)
          .value());
  EXPECT_EQ(
      13, stats_.gauge("test_prefix.min_rtt_msecs", Stats::Gauge::ImportMode::NeverImport).value());
}

TEST_F(GradientControllerTest, CancelLatencySample) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
)EOF";

  auto controller = makeController(yaml);

  for (int i = 1; i <= 5; ++i) {
    tryForward(controller, true);
    controller->recordLatencySample(std::chrono::milliseconds(i));
  }
  EXPECT_EQ(
      3, stats_.gauge("test_prefix.min_rtt_msecs", Stats::Gauge::ImportMode::NeverImport).value());
}

TEST_F(GradientControllerTest, SamplePercentileProcessTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
)EOF";

  auto controller = makeController(yaml);

  tryForward(controller, true);
  tryForward(controller, true);
  tryForward(controller, true);
  tryForward(controller, false);
  controller->cancelLatencySample();
  tryForward(controller, true);
  tryForward(controller, false);
}

TEST_F(GradientControllerTest, MinRTTBufferTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
  buffer:
    value: 50
)EOF";

  auto controller = makeController(yaml);
  EXPECT_EQ(controller->concurrencyLimit(), 3);

  // Force a minRTT of 5ms.
  advancePastMinRTTStage(controller, yaml, std::chrono::milliseconds(5));
  EXPECT_EQ(
      5, stats_.gauge("test_prefix.min_rtt_msecs", Stats::Gauge::ImportMode::NeverImport).value());

  // Ensure that the minRTT doesn't decrease due to the buffer added.
  for (int recalcs = 0; recalcs < 10; ++recalcs) {
    const auto last_concurrency = controller->concurrencyLimit();
    for (int i = 1; i <= 5; ++i) {
      tryForward(controller, true);
      // Recording sample that's technically higher than the minRTT, but the 50% buffer should
      // prevent the concurrency limit from decreasing.
      controller->recordLatencySample(std::chrono::milliseconds(6));
    }
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    EXPECT_GT(controller->concurrencyLimit(), last_concurrency);
  }
}

TEST_F(GradientControllerTest, ConcurrencyLimitBehaviorTestBasic) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
  buffer:
    value: 10
  min_concurrency: 7
)EOF";

  auto controller = makeController(yaml);
  EXPECT_EQ(controller->concurrencyLimit(), 7);

  // Force a minRTT of 5ms.
  advancePastMinRTTStage(controller, yaml, std::chrono::milliseconds(5));
  EXPECT_EQ(
      5, stats_.gauge("test_prefix.min_rtt_msecs", Stats::Gauge::ImportMode::NeverImport).value());

  // Ensure that the concurrency window increases on its own due to the headroom calculation with
  // the max gradient.
  time_system_.sleep(std::chrono::milliseconds(101));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_GE(controller->concurrencyLimit(), 7);
  EXPECT_LE(controller->concurrencyLimit() / 7.0, 2.0);

  // Make it seem as if the recorded latencies are consistently lower than the measured minRTT.
  // Ensure that it grows.
  for (int recalcs = 0; recalcs < 10; ++recalcs) {
    const auto last_concurrency = controller->concurrencyLimit();
    for (int i = 1; i <= 5; ++i) {
      tryForward(controller, true);
      controller->recordLatencySample(std::chrono::milliseconds(4));
    }
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    // Verify the minimum gradient.
    EXPECT_LE(last_concurrency, controller->concurrencyLimit());
    EXPECT_GE(static_cast<double>(last_concurrency) / controller->concurrencyLimit(), 0.5);
  }

  // Verify that the concurrency limit can now shrink as necessary.
  for (int recalcs = 0; recalcs < 10; ++recalcs) {
    const auto last_concurrency = controller->concurrencyLimit();
    for (int i = 1; i <= 5; ++i) {
      tryForward(controller, true);
      controller->recordLatencySample(std::chrono::milliseconds(6));
    }
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    EXPECT_LT(controller->concurrencyLimit(), last_concurrency);
    EXPECT_GE(controller->concurrencyLimit(), 7);
  }
}

TEST_F(GradientControllerTest, MinRTTReturnToPreviousLimit) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
)EOF";

  auto controller = makeController(yaml);
  EXPECT_EQ(controller->concurrencyLimit(), 3);

  // Get initial minRTT measurement out of the way.
  advancePastMinRTTStage(controller, yaml, std::chrono::milliseconds(5));

  // Force the limit calculation to run a few times from some measurements.
  for (int sample_iters = 0; sample_iters < 5; ++sample_iters) {
    const auto last_concurrency = controller->concurrencyLimit();
    for (int i = 1; i <= 5; ++i) {
      tryForward(controller, true);
      controller->recordLatencySample(std::chrono::milliseconds(4));
    }
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    // Verify the value is growing.
    EXPECT_GT(controller->concurrencyLimit(), last_concurrency);
  }

  const auto limit_val = controller->concurrencyLimit();

  // Wait until the minRTT recalculation is triggered again and verify the limit drops.
  time_system_.sleep(std::chrono::seconds(31));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(controller->concurrencyLimit(), 3);

  // 49 more requests should cause the minRTT to be done calculating.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(controller->concurrencyLimit(), 3);
    tryForward(controller, true);
    controller->recordLatencySample(std::chrono::milliseconds(13));
  }

  // Check that we restored the old concurrency limit value.
  EXPECT_EQ(limit_val, controller->concurrencyLimit());
}

TEST_F(GradientControllerTest, MinRTTRescheduleTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
)EOF";

  auto controller = makeController(yaml);
  EXPECT_EQ(controller->concurrencyLimit(), 3);

  // Get initial minRTT measurement out of the way.
  advancePastMinRTTStage(controller, yaml, std::chrono::milliseconds(5));

  // Force the limit calculation to run a few times from some measurements.
  for (int sample_iters = 0; sample_iters < 5; ++sample_iters) {
    const auto last_concurrency = controller->concurrencyLimit();
    for (int i = 1; i <= 5; ++i) {
      tryForward(controller, true);
      controller->recordLatencySample(std::chrono::milliseconds(4));
    }
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    // Verify the value is growing.
    EXPECT_GT(controller->concurrencyLimit(), last_concurrency);
  }

  // Wait until the minRTT recalculation is triggered again and verify the limit drops.
  time_system_.sleep(std::chrono::seconds(31));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(controller->concurrencyLimit(), 3);

  // Verify sample recalculation doesn't occur during the minRTT window.
  time_system_.sleep(std::chrono::milliseconds(101));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(controller->concurrencyLimit(), 3);
}

TEST_F(GradientControllerTest, NoSamplesTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.1s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 30s
  request_count: 5
)EOF";

  auto controller = makeController(yaml);
  EXPECT_EQ(controller->concurrencyLimit(), 3);

  // Get minRTT measurement out of the way.
  advancePastMinRTTStage(controller, yaml, std::chrono::milliseconds(5));

  // Force the limit calculation to run a few times from some measurements.
  for (int sample_iters = 0; sample_iters < 5; ++sample_iters) {
    const auto last_concurrency = controller->concurrencyLimit();
    for (int i = 1; i <= 5; ++i) {
      tryForward(controller, true);
      controller->recordLatencySample(std::chrono::milliseconds(4));
    }
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    // Verify the value is growing.
    EXPECT_GT(controller->concurrencyLimit(), last_concurrency);
  }

  // Now we make sure that the limit value doesn't change in the absence of samples.
  for (int sample_iters = 0; sample_iters < 5; ++sample_iters) {
    const auto old_limit = controller->concurrencyLimit();
    time_system_.sleep(std::chrono::milliseconds(101));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    EXPECT_EQ(old_limit, controller->concurrencyLimit());
  }
}

TEST_F(GradientControllerTest, TimerAccuracyTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.123s
min_rtt_calc_params:
  jitter:
    value: 10.0
  interval: 100s
  request_count: 5
)EOF";

  // Verify the configuration affects the timers that are kicked off.
  NiceMock<Event::MockDispatcher> fake_dispatcher;
  auto sample_timer = new Event::MockTimer();
  auto rtt_timer = new Event::MockTimer();

  // Expect the sample timer to trigger start immediately upon controller creation.
  EXPECT_CALL(fake_dispatcher, createTimer_(_))
      .Times(2)
      .WillOnce(Return(rtt_timer))
      .WillOnce(Return(sample_timer));
  EXPECT_CALL(*sample_timer, enableTimer(std::chrono::milliseconds(123), _));
  auto controller = std::make_shared<GradientController>(
      makeConfig(yaml, runtime_), fake_dispatcher, runtime_, "test_prefix.", stats_, random_);

  // Set the minRTT- this will trigger the timer for the next minRTT calculation.

  // Let's make sure the jitter value can't exceed the configured percentage as well by returning a
  // random value > 10% of the interval.
  EXPECT_CALL(random_, random()).WillOnce(Return(15000));
  EXPECT_CALL(*rtt_timer, enableTimer(std::chrono::milliseconds(105000), _));
  // Verify the sample timer is reset after the minRTT calculation occurs.
  EXPECT_CALL(*sample_timer, enableTimer(std::chrono::milliseconds(123), _));
  for (int i = 0; i < 6; ++i) {
    tryForward(controller, true);
    controller->recordLatencySample(std::chrono::milliseconds(5));
  }
}

TEST_F(GradientControllerTest, TimerAccuracyTestNoJitter) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile:
  value: 50
concurrency_limit_params:
  max_concurrency_limit:
  concurrency_update_interval: 0.123s
min_rtt_calc_params:
  jitter:
    value: 0.0
  interval: 45s
  request_count: 5
)EOF";

  // Verify the configuration affects the timers that are kicked off.
  NiceMock<Event::MockDispatcher> fake_dispatcher;
  auto sample_timer = new Event::MockTimer;
  auto rtt_timer = new Event::MockTimer;

  // Expect the sample timer to trigger start immediately upon controller creation.
  EXPECT_CALL(fake_dispatcher, createTimer_(_))
      .Times(2)
      .WillOnce(Return(rtt_timer))
      .WillOnce(Return(sample_timer));
  EXPECT_CALL(*sample_timer, enableTimer(std::chrono::milliseconds(123), _));
  auto controller = std::make_shared<GradientController>(
      makeConfig(yaml, runtime_), fake_dispatcher, runtime_, "test_prefix.", stats_, random_);

  // Set the minRTT- this will trigger the timer for the next minRTT calculation.
  EXPECT_CALL(*rtt_timer, enableTimer(std::chrono::milliseconds(45000), _));
  // Verify the sample timer is reset after the minRTT calculation occurs.
  EXPECT_CALL(*sample_timer, enableTimer(std::chrono::milliseconds(123), _));
  for (int i = 0; i < 6; ++i) {
    tryForward(controller, true);
    controller->recordLatencySample(std::chrono::milliseconds(5));
  }
}

} // namespace
} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
