#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "envoy/common/time.h"

#include "source/extensions/resource_monitors/adaptive_worker_capacity/dynamic_token_bucket.h"
#include "source/extensions/resource_monitors/adaptive_worker_capacity/refill_rate_controller.h"
#include "source/extensions/resource_monitors/adaptive_worker_capacity/worker_utilization_tracker.h"

#include "test/test_common/simulated_time_system.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {
namespace {

static_assert(!std::is_copy_constructible_v<RefillRateController>);
static_assert(!std::is_copy_assignable_v<RefillRateController>);
static_assert(!std::is_move_constructible_v<RefillRateController>);
static_assert(!std::is_move_assignable_v<RefillRateController>);

class FakeWorkerUtilizationTracker : public WorkerUtilizationTrackerInterface {
public:
  double sampleUtilization(MonotonicTime now) override {
    last_sampled_now_ = now;
    if (on_sample_) {
      on_sample_(now);
    }
    return utilization_;
  }

  void setUtilization(double u) { utilization_ = u; }

  void setOnSample(std::function<void(MonotonicTime)> on_sample) {
    on_sample_ = std::move(on_sample);
  }

  std::optional<MonotonicTime> lastSampledNow() const { return last_sampled_now_; }

private:
  double utilization_{0.0};
  std::optional<MonotonicTime> last_sampled_now_;
  std::function<void(MonotonicTime)> on_sample_;
};

class RefillRateControllerTest : public testing::Test {
protected:
  RefillRateControllerTest() {
    config_.refill_rate_adaptation_time = absl::Seconds(1);
    config_.target_worker_utilization = 0.80;
    config_.min_bucket_utilization_for_rate_increase = 0.05;

    bucket_ = std::make_unique<DynamicTokenBucket>(time_system_, absl::Seconds(1));
    controller_ =
        std::make_unique<RefillRateController>(config_, *bucket_, fake_tracker_, time_system_);
  }

  void initializeLimitAt(double rate) {
    SCOPED_TRACE("initializeLimitAt");
    fake_tracker_.setUtilization(0.90);
    const int count = static_cast<int>(rate);
    for (int i = 0; i < count; ++i) {
      bucket_->onLoadAccepted();
    }
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    controller_->updateRefillRate();
    EXPECT_EQ(RefillRateController::Mode::Normal, controller_->mode());
    EXPECT_DOUBLE_EQ(rate, bucket_->refillRate());
  }

  Event::SimulatedTimeSystem time_system_;
  RefillRateControllerOptions config_;
  FakeWorkerUtilizationTracker fake_tracker_;
  std::unique_ptr<DynamicTokenBucket> bucket_;
  std::unique_ptr<RefillRateController> controller_;
};

TEST_F(RefillRateControllerTest, InitialProperties) {
  EXPECT_EQ(absl::Seconds(1), controller_->config().refill_rate_adaptation_time);
  EXPECT_DOUBLE_EQ(0.80, controller_->config().target_worker_utilization);
  EXPECT_DOUBLE_EQ(0.05, controller_->config().min_bucket_utilization_for_rate_increase);
  EXPECT_EQ(RefillRateController::Mode::InitializeLimit, controller_->mode());
  EXPECT_DOUBLE_EQ(0.0, controller_->lastConsumptionRate());
  EXPECT_EQ(time_system_.monotonicTime(), controller_->lastUpdateTime());
  EXPECT_DOUBLE_EQ(0.0, controller_->lastWorkerUtilization());
  EXPECT_DOUBLE_EQ(0.0, controller_->lastBucketUtilization());
  EXPECT_DOUBLE_EQ(0.0, bucket_->refillRate());
  EXPECT_EQ(std::nullopt, bucket_->maxTokens());
  EXPECT_DOUBLE_EQ(0.0, bucket_->getResourceUsage().resource_pressure_);
}

TEST_F(RefillRateControllerTest, ZeroUtilizationDoublesAtFullRate) {
  initializeLimitAt(100.0);

  // Simulate worker utilization = 0.0 (normalized_error = 1.0 -> gain = 1.0).
  fake_tracker_.setUtilization(0.0);

  // Consume tokens so bucket utilization > min_bucket_utilization_for_rate_increase (0.05).
  for (int i = 0; i < 50; ++i) {
    bucket_->onLoadAccepted();
  }

  // Advance time by 1 second (equal to adaptation time).
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  controller_->updateRefillRate();

  // Rate should have doubled from 100.0 to 200.0.
  EXPECT_DOUBLE_EQ(200.0, bucket_->refillRate());
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_DOUBLE_EQ(200.0, *bucket_->maxTokens());
}

TEST_F(RefillRateControllerTest, FullUtilizationHalvesAtFullRate) {
  initializeLimitAt(100.0);

  // Simulate worker utilization = 1.0 (normalized_error = 1.0 -> gain = 1.0).
  fake_tracker_.setUtilization(1.0);

  // Advance time by 1 second (equal to adaptation time).
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  controller_->updateRefillRate();

  // Rate should have halved from 100.0 to 50.0.
  EXPECT_DOUBLE_EQ(50.0, bucket_->refillRate());
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_DOUBLE_EQ(50.0, *bucket_->maxTokens());
}

TEST_F(RefillRateControllerTest, TargetUtilizationPreservesRefillRate) {
  initializeLimitAt(100.0);

  // Simulate worker utilization equal to target.
  fake_tracker_.setUtilization(config_.target_worker_utilization);

  // Even if bucket utilization is high:
  for (int i = 0; i < 80; ++i) {
    bucket_->onLoadAccepted();
  }

  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  // Rate should remain 100.0.
  EXPECT_DOUBLE_EQ(100.0, bucket_->refillRate());
}

TEST_F(RefillRateControllerTest, LogLinearIncreaseBelowTarget) {
  initializeLimitAt(100.0);

  // u = 0.70, target = 0.80 -> normalized_error = (0.80 - 0.70) / (1.0 - 0.80) = 0.5.
  // After 1s (adaptation time): factor = 2^0.5 ~= 1.414214 -> rate ~= 141.4214.
  fake_tracker_.setUtilization(0.70);
  for (int i = 0; i < 50; ++i) {
    bucket_->onLoadAccepted();
  }

  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  EXPECT_NEAR(141.4214, bucket_->refillRate(), 1e-3);
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_NEAR(141.4214, *bucket_->maxTokens(), 1e-3);
}

TEST_F(RefillRateControllerTest, SymmetricErrorAroundTargetCancelsOut) {
  initializeLimitAt(100.0);

  // Tick 1: u = 0.90 (+0.10 above target 0.80).
  // normalized_error = 0.10 / 0.20 = 0.5. Rate decreases by factor 2^-0.5.
  fake_tracker_.setUtilization(0.90);
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();
  EXPECT_NEAR(70.7107, bucket_->refillRate(), 1e-3);

  // Tick 2: u = 0.70 (-0.10 below target 0.80).
  // normalized_error = 0.10 / 0.20 = 0.5.
  // Ensure bucket utilization > min_bucket_utilization_for_rate_increase (0.05).
  fake_tracker_.setUtilization(0.70);
  for (int i = 0; i < 50; ++i) {
    bucket_->onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  // Symmetric errors cancel out: 100.0 * 2^-0.5 * 2^0.5 = 100.0.
  EXPECT_NEAR(100.0, bucket_->refillRate(), 1e-3);
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_NEAR(100.0, *bucket_->maxTokens(), 1e-3);
}

TEST_F(RefillRateControllerTest, LogLinearDecreaseAboveTarget) {
  initializeLimitAt(100.0);

  // u = 0.90, target = 0.80 -> normalized_error = (0.90 - 0.80) / (1.0 - 0.80) = 0.5.
  // After 1s (adaptation time): factor = 2^-0.5 ~= 0.707107 -> rate ~= 70.7107.
  fake_tracker_.setUtilization(0.90);

  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  EXPECT_NEAR(70.7107, bucket_->refillRate(), 1e-3);
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_NEAR(70.7107, *bucket_->maxTokens(), 1e-3);
}

TEST_F(RefillRateControllerTest, IdleUnderutilizationDoesNotIncreaseRefillRate) {
  initializeLimitAt(100.0);

  // Simulate worker utilization = 0.10 (< target 0.80).
  fake_tracker_.setUtilization(0.10);

  // No tokens consumed -> bucket utilization = 0.0 (<= 0.05).
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  controller_->updateRefillRate();

  // Rate should remain unchanged at 100.0.
  EXPECT_DOUBLE_EQ(100.0, bucket_->refillRate());
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_DOUBLE_EQ(100.0, *bucket_->maxTokens());
}

TEST_F(RefillRateControllerTest, ZeroDeltaTimeDoesNothing) {
  initializeLimitAt(100.0);

  // Run one normal-mode tick with token consumption so lastBucketUtilization() is non-zero before
  // testing zero elapsed time.
  fake_tracker_.setUtilization(0.80);
  for (int i = 0; i < 50; ++i) {
    bucket_->onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  const double prev_worker_util = controller_->lastWorkerUtilization();
  const double prev_bucket_util = controller_->lastBucketUtilization();
  const double prev_consumption_rate = controller_->lastConsumptionRate();
  ASSERT_GT(prev_worker_util, 0.0);
  ASSERT_GT(prev_bucket_util, 0.0);
  ASSERT_GT(prev_consumption_rate, 0.0);

  fake_tracker_.setUtilization(0.50);
  bucket_->onLoadAccepted();

  // No time elapsed.
  controller_->updateRefillRate();
  EXPECT_DOUBLE_EQ(100.0, bucket_->refillRate());
  EXPECT_DOUBLE_EQ(prev_worker_util, controller_->lastWorkerUtilization());
  EXPECT_DOUBLE_EQ(prev_bucket_util, controller_->lastBucketUtilization());
  EXPECT_DOUBLE_EQ(prev_consumption_rate, controller_->lastConsumptionRate());

  // Verify that the zero-delta call preserved state and accepted tokens for the next
  // positive-duration update.
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();
  EXPECT_DOUBLE_EQ(0.50, controller_->lastWorkerUtilization());
  EXPECT_DOUBLE_EQ(1.0, controller_->lastConsumptionRate());
}

TEST_F(RefillRateControllerTest, InitializeLimitRemainsUnthrottledWhenUnderloaded) {
  // Simulate worker under target utilization.
  fake_tracker_.setUtilization(0.50);

  // Even with many accepted tokens (e.g. 500 tokens):
  for (int i = 0; i < 500; ++i) {
    bucket_->onLoadAccepted();
  }

  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  EXPECT_EQ(RefillRateController::Mode::InitializeLimit, controller_->mode());
  EXPECT_DOUBLE_EQ(0.0, bucket_->refillRate());
  EXPECT_EQ(std::nullopt, bucket_->maxTokens());
  EXPECT_DOUBLE_EQ(0.0, bucket_->getResourceUsage().resource_pressure_);
}

TEST_F(RefillRateControllerTest, InitializeLimitStaysInModeWhenOverloadHasZeroConsumption) {
  // Worker is overloaded but 0 tokens have been accepted.
  fake_tracker_.setUtilization(0.90);

  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  EXPECT_EQ(RefillRateController::Mode::InitializeLimit, controller_->mode());
  EXPECT_DOUBLE_EQ(0.0, controller_->lastConsumptionRate());
  EXPECT_DOUBLE_EQ(0.0, bucket_->refillRate());
}

TEST_F(RefillRateControllerTest, InitializeLimitSnapsToObservedRateOnFirstOverload) {
  // Worker is overloaded (0.90 > 0.80) with 60 accepted tokens over 1s.
  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 60; ++i) {
    bucket_->onLoadAccepted();
  }

  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  // Mode transitions to Normal and refill rate snaps to observed consumption rate.
  EXPECT_EQ(RefillRateController::Mode::Normal, controller_->mode());
  EXPECT_DOUBLE_EQ(60.0, controller_->lastConsumptionRate());
  EXPECT_DOUBLE_EQ(60.0, bucket_->refillRate());
  ASSERT_TRUE(bucket_->maxTokens().has_value());
  EXPECT_DOUBLE_EQ(60.0, *bucket_->maxTokens());

  // Subsequent cycle in Normal mode with overload performs normal log-linear decrease.
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();
  // u = 0.90, target = 0.80 -> error = 0.5, log-linear factor = 2^-0.5 ~= 0.707107.
  // 60.0 * 0.707107 ~= 42.4264.
  EXPECT_NEAR(42.4264, bucket_->refillRate(), 1e-3);
}

TEST_F(RefillRateControllerTest, SubUnitRefillRateRecoversWhenWorkerBecomesUnderutilized) {
  // Initialize at 1.0 token/s (max_tokens = 1.0 for 1s bucket).
  initializeLimitAt(1.0);

  // Sustained overload (utilization = 1.0) for 1s halves the refill rate to 0.5 tokens/s, while
  // max_tokens stays clamped at >= 1.0.
  fake_tracker_.setUtilization(1.0);
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();
  ASSERT_DOUBLE_EQ(0.5, bucket_->refillRate());
  ASSERT_DOUBLE_EQ(1.0, *bucket_->maxTokens());

  // Now the overload subsides completely (worker utilization = 0.0) while requests continue
  // arriving. Simulate LoadShedPoint behavior: a request is only accepted (calling
  // onLoadAccepted()) if getResourceUsage() < 1.0.
  fake_tracker_.setUtilization(0.0);
  for (int tick = 0; tick < 10; ++tick) {
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    if (bucket_->getResourceUsage().resource_pressure_ < 1.0) {
      bucket_->onLoadAccepted();
    }
    controller_->updateRefillRate();
  }

  // After 10s of 0% worker utilization and continuous incoming requests, the controller and bucket
  // recover above 0.5 tokens/s and are not wedged at 1.0 resource pressure.
  EXPECT_GT(bucket_->refillRate(), 0.5);
  EXPECT_LT(bucket_->getResourceUsage().resource_pressure_, 1.0);
}

TEST_F(RefillRateControllerTest, MidTickClockAdvanceUsesSingleSnapshotTimestamp) {
  const MonotonicTime t0 = time_system_.monotonicTime();
  for (int i = 0; i < 100; ++i) {
    bucket_->onLoadAccepted();
  }

  // Simulate OS preemption advancing the monotonic clock inside sampleUtilization(now).
  std::chrono::milliseconds mid_tick_advance{250};
  fake_tracker_.setOnSample([&](MonotonicTime) {
    if (mid_tick_advance.count() > 0) {
      time_system_.advanceTimeWait(mid_tick_advance);
    }
  });

  // Tick 1 (InitializeLimit -> Normal): advances 1.0s to t0 + 1.0s; mid-tick preemption moves
  // wall clock to t0 + 1.25s. All cold-path calls must use now = t0 + 1.0s.
  fake_tracker_.setUtilization(0.90);
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  ASSERT_TRUE(fake_tracker_.lastSampledNow().has_value());
  EXPECT_EQ(*fake_tracker_.lastSampledNow(), controller_->lastUpdateTime());
  EXPECT_EQ(*fake_tracker_.lastSampledNow(), t0 + std::chrono::seconds(1));
  EXPECT_EQ(RefillRateController::Mode::Normal, controller_->mode());
  EXPECT_DOUBLE_EQ(100.0, controller_->lastConsumptionRate());
  EXPECT_DOUBLE_EQ(100.0, bucket_->refillRate());
  EXPECT_DOUBLE_EQ(100.0, bucket_->tokenCount());

  // Tick 2 (increaseRefillRate): consumes 80 tokens (leaving 20), runs 250ms later at t0 + 1.5s
  // (500ms after Tick 1's snapshot), while mid-tick preemption moves wall clock to t0 + 2.0s.
  // Refills 50 tokens (20 -> 70), rate scales by exp2(0.5), tokenCount = 70 * exp2(0.5)
  // (not 100 * exp2(0.5) if wall clock t0 + 2.0s were used).
  fake_tracker_.setUtilization(0.60);
  for (int i = 0; i < 80; ++i) {
    bucket_->onLoadAccepted();
  }
  mid_tick_advance = std::chrono::milliseconds(500);
  time_system_.advanceTimeWait(std::chrono::milliseconds(250));
  controller_->updateRefillRate();

  const double expected_rate2 = 100.0 * std::exp2(0.5);
  EXPECT_EQ(*fake_tracker_.lastSampledNow(), t0 + std::chrono::milliseconds(1500));
  EXPECT_EQ(controller_->lastUpdateTime(), t0 + std::chrono::milliseconds(1500));
  EXPECT_NEAR(0.55, controller_->lastBucketUtilization(), 1e-4);
  EXPECT_NEAR(160.0, controller_->lastConsumptionRate(), 1e-4);
  EXPECT_NEAR(expected_rate2, bucket_->refillRate(), 1e-4);
  EXPECT_NEAR(70.0 * std::exp2(0.5), bucket_->tokenCount(), 1e-4);

  // Tick 3 (decreaseRefillRate): consumes 80 tokens, runs 100ms later at t0 + 2.1s (600ms after
  // Tick 2's snapshot), while mid-tick preemption moves wall clock to t0 + 2.6s.
  // Pre-scale tokens at t0 + 2.1s = (70 * exp2(0.5) - 80) + 0.6 * 100 * exp2(0.5)
  // = 130 * exp2(0.5) - 80 (~103.8478 < expected_rate2), scaled by exp2(-0.3).
  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 80; ++i) {
    bucket_->onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::milliseconds(100));
  controller_->updateRefillRate();

  const double expected_rate3 = expected_rate2 * std::exp2(-0.3);
  const double expected_tokens3 = (130.0 * std::exp2(0.5) - 80.0) * std::exp2(-0.3);
  EXPECT_EQ(*fake_tracker_.lastSampledNow(), t0 + std::chrono::milliseconds(2100));
  EXPECT_EQ(controller_->lastUpdateTime(), t0 + std::chrono::milliseconds(2100));
  EXPECT_NEAR(80.0 / expected_rate2, controller_->lastBucketUtilization(), 1e-4);
  EXPECT_NEAR(80.0 / 0.6, controller_->lastConsumptionRate(), 1e-4);
  EXPECT_NEAR(expected_rate3, bucket_->refillRate(), 1e-4);
  EXPECT_NEAR(expected_tokens3, bucket_->tokenCount(), 1e-4);
}

TEST_F(RefillRateControllerTest, IncreaseRateFactorCappedAtMaxIncreaseFactor) {
  RefillRateControllerOptions fast_config;
  fast_config.refill_rate_adaptation_time = absl::Microseconds(500);
  fast_config.target_worker_utilization = 0.80;
  fast_config.min_bucket_utilization_for_rate_increase = 0.05;

  DynamicTokenBucket bucket(time_system_, absl::Seconds(1));
  RefillRateController controller(fast_config, bucket, fake_tracker_, time_system_);

  // Initialize at 100.0 tokens/sec.
  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 100; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();
  ASSERT_DOUBLE_EQ(100.0, bucket.refillRate());

  // Consume 50 tokens -> bucket utilization = 0.125 (> 0.05) over 1s.
  // With delta_time = 1.0s and adaptation_time = 500us at u = 0.0 (normalized_error = 1.0),
  // uncapped exponent = 2000.0 (> 1024), which would overflow double in std::exp2 unless clamped
  // at kMaxIncreaseExponent (5.0 -> 32x) beforehand.
  fake_tracker_.setUtilization(0.0);
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();

  EXPECT_DOUBLE_EQ(3200.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(3200.0, *bucket.maxTokens());
}

TEST_F(RefillRateControllerTest, ExactMinBucketUtilizationBoundaryDoesNotIncreaseRate) {
  // Choose binary-exact threshold 0.125 (1/8) and initial rate 128.0 (power of 2) so consuming 64
  // tokens over 1.0s produces bit-exact bucket utilization = 16.0 / (1.0 * 128.0) = 0.125.
  RefillRateControllerOptions boundary_config;
  boundary_config.refill_rate_adaptation_time = absl::Seconds(1);
  boundary_config.target_worker_utilization = 0.80;
  boundary_config.min_bucket_utilization_for_rate_increase = 0.125;

  DynamicTokenBucket bucket(time_system_, absl::Seconds(1));
  RefillRateController controller(boundary_config, bucket, fake_tracker_, time_system_);

  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 128; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();
  ASSERT_DOUBLE_EQ(128.0, bucket.refillRate());

  // Exact equality (0.125 == 0.125) must NOT trigger a rate increase despite 0.0 worker util.
  fake_tracker_.setUtilization(0.0);
  for (int i = 0; i < 64; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();

  EXPECT_DOUBLE_EQ(0.125, controller.lastBucketUtilization());
  EXPECT_DOUBLE_EQ(128.0, bucket.refillRate());

  // Strictly exceeding the threshold (65 tokens -> > 0.125) doubles the rate to 256.0.
  for (int i = 0; i < 65; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();

  EXPECT_GT(controller.lastBucketUtilization(), 0.125);
  EXPECT_DOUBLE_EQ(256.0, bucket.refillRate());
}

TEST_F(RefillRateControllerTest, ExtremeOverloadDurationClampsDecreaseInsteadOfUnderflowingToNoOp) {
  RefillRateControllerOptions fast_config;
  fast_config.refill_rate_adaptation_time = absl::Milliseconds(10);
  fast_config.target_worker_utilization = 0.80;
  fast_config.min_bucket_utilization_for_rate_increase = 0.05;

  DynamicTokenBucket bucket(time_system_, absl::Seconds(1));
  RefillRateController controller(fast_config, bucket, fake_tracker_, time_system_);

  // Initialize at 1000.0 tokens/sec.
  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 1000; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();
  ASSERT_DOUBLE_EQ(1000.0, bucket.refillRate());

  // 11s at 10ms adaptation time = 1100 periods: uncapped exp2(-1100.0) would underflow to 0.0
  // and hit scaleRefillRate's factor <= 0.0 early return instead of decaying down to 1e-3.
  fake_tracker_.setUtilization(1.0);
  time_system_.advanceTimeWait(std::chrono::seconds(11));
  controller.updateRefillRate();

  EXPECT_DOUBLE_EQ(1e-3, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(1.0, *bucket.maxTokens());
}

TEST_F(RefillRateControllerTest,
       InitializeLimitAtExactTargetStaysUnthrottledAndResetsWindowBeforeOverload) {
  // Exact equality with target_worker_utilization must remain in InitializeLimit while still
  // draining this window's accepted tokens so they do not carry over to a later overload tick.
  fake_tracker_.setUtilization(config_.target_worker_utilization);
  for (int i = 0; i < 500; ++i) {
    bucket_->onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  EXPECT_EQ(RefillRateController::Mode::InitializeLimit, controller_->mode());
  EXPECT_DOUBLE_EQ(0.0, bucket_->refillRate());
  EXPECT_EQ(std::nullopt, bucket_->maxTokens());
  EXPECT_DOUBLE_EQ(500.0, controller_->lastConsumptionRate());
  EXPECT_DOUBLE_EQ(0.80, controller_->lastWorkerUtilization());

  // Next window at u = 0.90 snaps strictly to this window's 60 tokens/sec (not 560 tokens/sec).
  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 60; ++i) {
    bucket_->onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller_->updateRefillRate();

  EXPECT_EQ(RefillRateController::Mode::Normal, controller_->mode());
  EXPECT_DOUBLE_EQ(60.0, controller_->lastConsumptionRate());
  EXPECT_DOUBLE_EQ(60.0, bucket_->refillRate());
}

TEST_F(RefillRateControllerTest, NonUnitAdaptationTimeScalesBothDecreaseAndIncreaseRates) {
  RefillRateControllerOptions slow_config;
  slow_config.refill_rate_adaptation_time = absl::Seconds(2);
  slow_config.target_worker_utilization = 0.80;
  slow_config.min_bucket_utilization_for_rate_increase = 0.05;

  DynamicTokenBucket bucket(time_system_, absl::Seconds(1));
  RefillRateController controller(slow_config, bucket, fake_tracker_, time_system_);

  fake_tracker_.setUtilization(0.90);
  for (int i = 0; i < 100; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();
  ASSERT_DOUBLE_EQ(100.0, bucket.refillRate());

  // 1s at u = 1.0 (normalized_error = 1.0) with 2s adaptation_time scales by exp2(-0.5).
  fake_tracker_.setUtilization(1.0);
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();
  EXPECT_NEAR(100.0 * std::exp2(-0.5), bucket.refillRate(), 1e-4);

  // 1s at u = 0.0 (normalized_error = 1.0) with 2s adaptation_time and consumed load (> 0.05
  // bucket utilization) scales by exp2(+0.5) back to 100.0.
  fake_tracker_.setUtilization(0.0);
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  controller.updateRefillRate();
  EXPECT_NEAR(100.0, bucket.refillRate(), 1e-4);
}

TEST_F(RefillRateControllerTest, MinBucketUtilizationInclusiveEndpointsZeroAndOne) {
  // Lower endpoint 0.0: equality (0.0 == 0.0) stays unchanged, while any positive depletion (> 0.0)
  // triggers a rate increase.
  {
    RefillRateControllerOptions zero_config;
    zero_config.refill_rate_adaptation_time = absl::Seconds(1);
    zero_config.target_worker_utilization = 0.80;
    zero_config.min_bucket_utilization_for_rate_increase = 0.0;

    DynamicTokenBucket bucket(time_system_, absl::Seconds(1));
    RefillRateController controller(zero_config, bucket, fake_tracker_, time_system_);

    fake_tracker_.setUtilization(0.90);
    for (int i = 0; i < 100; ++i) {
      bucket.onLoadAccepted();
    }
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    controller.updateRefillRate();
    ASSERT_DOUBLE_EQ(100.0, bucket.refillRate());

    fake_tracker_.setUtilization(0.0);
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    controller.updateRefillRate();
    EXPECT_DOUBLE_EQ(0.0, controller.lastBucketUtilization());
    EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());

    bucket.onLoadAccepted();
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    controller.updateRefillRate();
    EXPECT_GT(controller.lastBucketUtilization(), 0.0);
    EXPECT_DOUBLE_EQ(200.0, bucket.refillRate());
  }

  // Upper endpoint 1.0: accepted by constructor assertions and never increases rate because
  // refilling makes bucket utilization strictly <= 1.0.
  {
    RefillRateControllerOptions one_config;
    one_config.refill_rate_adaptation_time = absl::Seconds(1);
    one_config.target_worker_utilization = 0.80;
    one_config.min_bucket_utilization_for_rate_increase = 1.0;

    DynamicTokenBucket bucket(time_system_, absl::Seconds(1));
    RefillRateController controller(one_config, bucket, fake_tracker_, time_system_);

    fake_tracker_.setUtilization(0.90);
    for (int i = 0; i < 100; ++i) {
      bucket.onLoadAccepted();
    }
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    controller.updateRefillRate();
    ASSERT_DOUBLE_EQ(100.0, bucket.refillRate());

    fake_tracker_.setUtilization(0.0);
    for (int i = 0; i < 100; ++i) {
      bucket.onLoadAccepted();
    }
    time_system_.advanceTimeWait(std::chrono::milliseconds(10));
    controller.updateRefillRate();
    EXPECT_NEAR(0.995, controller.lastBucketUtilization(), 1e-4);
    EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());
  }
}

} // namespace
} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
