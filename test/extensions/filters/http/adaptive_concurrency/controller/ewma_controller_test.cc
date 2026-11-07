#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"

#include "source/extensions/filters/http/adaptive_concurrency/controller/ewma_controller.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {
namespace {

using namespace std::chrono_literals;

class EwmaControllerTest : public testing::Test {
protected:
  EwmaControllerTest()
      : api_(Api::createApiForTest(time_)), dispatcher_(api_->allocateDispatcher("ewma_test")) {
    time_.setMonotonicTime(1h);
  }

  void start(const std::string& yaml) {
    EwmaController::Config proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    auto result = EwmaController::create(proto, *dispatcher_, "test.", *stats_.rootScope(), time_);
    ASSERT_TRUE(result.ok()) << result.status();
    controller_ = std::move(result.value());
  }

  void record(std::chrono::microseconds latency, uint32_t count = 1) {
    for (uint32_t i = 0; i < count; ++i) {
      ASSERT_EQ(RequestForwardingAction::Forward, controller_->forwardingDecision());
    }
    for (uint32_t i = 0; i < count; ++i) {
      controller_->recordLatencySample(time_.monotonicTime() - latency);
    }
  }

  void tick(std::chrono::milliseconds interval = 1s) {
    time_.advanceTimeAndRun(interval, *dispatcher_, Event::Dispatcher::RunType::Block);
  }
  uint64_t gauge(const std::string& name) {
    return stats_.gauge("test." + name, Stats::Gauge::ImportMode::NeverImport).value();
  }

  Stats::TestUtil::TestStore stats_;
  Event::SimulatedTimeSystem time_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<EwmaController> controller_;
};

TEST_F(EwmaControllerTest, BoundsAdmissionCancellationAndIdleWindows) {
  start("{}");
  EXPECT_EQ(3, controller_->concurrencyLimit());
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(RequestForwardingAction::Forward, controller_->forwardingDecision());
  }
  EXPECT_EQ(RequestForwardingAction::Block, controller_->forwardingDecision());
  EXPECT_EQ(1, stats_.counter("test.rq_blocked").value());
  for (int i = 0; i < 3; ++i) {
    controller_->cancelLatencySample();
  }
  tick();
  EXPECT_EQ(0, gauge("baseline_rtt_msecs"));
  // Even sub-microsecond samples yield a finite gradient.
  record(0us, 3);
  tick();
  EXPECT_EQ(1000, gauge("gradient"));
  EXPECT_EQ(3, controller_->concurrencyLimit());
  // Fractional increases are retained across windows.
  record(0us, 3);
  tick();
  EXPECT_EQ(4, controller_->concurrencyLimit());
}

TEST_F(EwmaControllerTest, WindowMedianRejectsIsolatedOutlier) {
  start("initial_concurrency: 100");
  record(2ms, 99);
  record(5s);
  tick();
  EXPECT_EQ(2, gauge("sample_rtt_msecs"));
  EXPECT_EQ(2, gauge("baseline_rtt_msecs"));
  record(2ms, 99);
  record(5s);
  tick();
  EXPECT_EQ(1000, gauge("gradient"));
  EXPECT_GE(controller_->concurrencyLimit(), 100);
}

TEST_F(EwmaControllerTest, CustomPercentileWarmupAndHalfLife) {
  start("sample_aggregate_percentile: {value: 90}\nwarmup_windows: 2\nhalf_life: 1s");
  record(2ms);
  record(10ms, 2);
  tick();
  EXPECT_NEAR(10, gauge("sample_rtt_msecs"), 1);
  for (int i = 0; i < 5; ++i) {
    tick();
  }
  record(30ms);
  tick();
  EXPECT_NEAR(20, gauge("baseline_rtt_msecs"), 1);
  for (int i = 0; i < 5; ++i) {
    tick();
  }
  record(60ms);
  tick();
  EXPECT_NEAR(40, gauge("baseline_rtt_msecs"), 1);
}

TEST_F(EwmaControllerTest, WarmupAveragesWindowPercentilesBeforeAcceleratedDecay) {
  start("warmup_windows: 2");
  record(100ms);
  tick();
  const double slow = gauge("sample_rtt_msecs");
  record(2ms);
  tick();
  EXPECT_NEAR((slow + gauge("sample_rtt_msecs")) / 2, gauge("baseline_rtt_msecs"), 1);
}

TEST_F(EwmaControllerTest, SmoothedAndBoundedLimitOnlyChangesUnderLoad) {
  start("min_concurrency_limit: 98\ninitial_concurrency: 100\nmax_concurrency_limit: 101\n"
        "warmup_windows: 1\nbuffer: {value: 0}");
  record(10ms);
  tick();
  record(1s);
  tick();
  EXPECT_EQ(500, gauge("gradient"));
  EXPECT_EQ(100, controller_->concurrencyLimit()); // Application-limited window.
  record(1s, 100);
  tick();
  EXPECT_EQ(98, controller_->concurrencyLimit());
  for (int i = 0; i < 8; ++i) {
    record(10ms, 98);
    tick();
  }
  EXPECT_EQ(101, controller_->concurrencyLimit());
  EXPECT_EQ(101, gauge("concurrency_limit"));
}

TEST_F(EwmaControllerTest, HalfUtilizationBoundaryAndSmoothedDecrease) {
  start("initial_concurrency: 100\nwarmup_windows: 1");
  record(10ms, 49);
  tick();
  record(100ms, 49);
  tick();
  EXPECT_EQ(100, controller_->concurrencyLimit());
  record(100ms, 50);
  tick();
  // The raw target is 100 * 0.5 + 4. Apply only 20% of that decrease.
  EXPECT_EQ(90, controller_->concurrencyLimit());
}

TEST_F(EwmaControllerTest, AbsoluteAndRelativeBufferDoNotStack) {
  start("buffer: {value: 100}\nmin_latency_delta: 0.020s\nwarmup_windows: 1\n"
        "initial_concurrency: 100\nhalf_life: 3600s");
  record(4ms, 100);
  tick();
  record(30ms, 100);
  tick();
  // (4 + max(4, 20)) / 30, not (4 + 4 + 20) / 30.
  EXPECT_NEAR(800, gauge("gradient"), 25);
  EXPECT_NEAR(96.8, controller_->concurrencyLimit(), 2);
}

TEST_F(EwmaControllerTest, FastBaselineRecoveryAfterTwentyMinuteBrownout) {
  start("concurrency_update_interval: 5s\nhalf_life: 300s\nwarmup_windows: 1");
  record(10ms);
  tick(5s);
  const double healthy = gauge("sample_rtt_msecs");
  for (int i = 0; i < 240; ++i) {
    record(100ms);
    tick(5s);
  }
  // Four half-lives: 1/16 of the healthy RTT and 15/16 of the measured slow RTT.
  const double slow = gauge("sample_rtt_msecs");
  EXPECT_NEAR((healthy + 15 * slow) / 16, gauge("baseline_rtt_msecs"), 1);
  // After only half a half-life, the ordinary EWMA would still be above 65 ms.
  for (int i = 0; i < 30; ++i) {
    record(10ms);
    tick(5s);
  }
  EXPECT_LE(gauge("baseline_rtt_msecs"), 21);
  EXPECT_GE(gauge("baseline_rtt_msecs"), 10);
}

TEST_F(EwmaControllerTest, RejectsInconsistentOrInvalidParameters) {
  for (const std::string yaml :
       {"min_concurrency_limit: 5\nmax_concurrency_limit: 4",
        "min_concurrency_limit: 5\ninitial_concurrency: 4", "initial_concurrency: 1001"}) {
    EwmaController::Config proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    auto result = EwmaController::create(proto, *dispatcher_, "test.", *stats_.rootScope(), time_);
    EXPECT_EQ(absl::StatusCode::kInvalidArgument, result.status().code());
  }
  for (const std::string yaml :
       {"concurrency_update_interval: 0.0001s", "half_life: 0s", "warmup_windows: 0",
        "initial_concurrency: 0", "min_latency_delta: -0.001s",
        "sample_aggregate_percentile: {value: 101}"}) {
    EwmaController::Config proto;
    EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto), ProtoValidationException);
  }
}

} // namespace
} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
