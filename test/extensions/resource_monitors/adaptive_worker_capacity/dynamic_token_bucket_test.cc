#include <chrono>
#include <limits>
#include <optional>

#include "source/extensions/resource_monitors/adaptive_worker_capacity/dynamic_token_bucket.h"

#include "test/test_common/simulated_time_system.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace AdaptiveWorkerCapacity {
namespace {

TEST(DynamicTokenBucketTest, UninitializedInitialState) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));

  EXPECT_DOUBLE_EQ(0.0, bucket.refillRate());
  EXPECT_EQ(std::nullopt, bucket.maxTokens());
  EXPECT_DOUBLE_EQ(0.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());

  // Even when uninitialized, accepted tokens are recorded for consumption rate.
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(0.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());

  bucket.scaleRefillRate(2.0);
  EXPECT_DOUBLE_EQ(0.0, bucket.refillRate());
  EXPECT_EQ(std::nullopt, bucket.maxTokens());

  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());
  EXPECT_DOUBLE_EQ(100.0, bucket.popTokenConsumptionRate());
}

TEST(DynamicTokenBucketTest, InitialStateIsFull) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(2));
  bucket.setRefillRate(100.0);

  EXPECT_EQ(absl::Seconds(2), bucket.bucketSize());
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(200.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(200.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);
}

TEST(DynamicTokenBucketTest, TokenConsumptionIncreasesPressure) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  bucket.onLoadAccepted();
  EXPECT_DOUBLE_EQ(99.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.01, bucket.getResourceUsage().resource_pressure_);

  for (int i = 0; i < 49; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(50.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.50, bucket.getResourceUsage().resource_pressure_);

  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(0.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(1.0, bucket.getResourceUsage().resource_pressure_);

  // Consuming when empty clamps to 0 tokens and 1.0 pressure.
  bucket.onLoadAccepted();
  EXPECT_DOUBLE_EQ(0.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(1.0, bucket.getResourceUsage().resource_pressure_);
}

TEST(DynamicTokenBucketTest, FractionalTokenBelowOneReportsFullPressure) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  // Consume all 100 tokens.
  for (int i = 0; i < 100; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(0.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(1.0, bucket.getResourceUsage().resource_pressure_);

  // Advance time by 5ms: 5ms * 100 tokens/s = 0.5 tokens.
  // With fewer than 1.0 tokens remaining, a full request cannot be served,
  // so getResourceUsage() must report full resource pressure (1.0).
  time_system.advanceTimeWait(std::chrono::milliseconds(5));
  EXPECT_DOUBLE_EQ(1.0, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(0.5, bucket.tokenCount());

  // Advance time by another 6ms (total 11ms -> 1.1 tokens).
  // Now that at least 1.0 token is available, pressure reflects consumed
  // fraction: (100 - 1.1) / 100 = 0.989.
  time_system.advanceTimeWait(std::chrono::milliseconds(6));
  EXPECT_DOUBLE_EQ(0.989, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(1.1, bucket.tokenCount());
}

TEST(DynamicTokenBucketTest, RefillOverTimeRestoresCapacity) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(50.0, bucket.tokenCount());

  // Advance time by 200ms -> 20 tokens refilled.
  time_system.advanceTimeWait(std::chrono::milliseconds(200));
  EXPECT_DOUBLE_EQ(0.30, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(70.0, bucket.tokenCount());

  // Advance time by another 500ms -> 50 tokens refilled, capped at 100 max tokens.
  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(100.0, bucket.tokenCount());
}

TEST(DynamicTokenBucketTest, BucketUtilizationCalculatesConsumedAreaAccurately) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  // If completely idle, bucket utilization is 0.0.
  time_system.advanceTimeWait(std::chrono::milliseconds(200));
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());

  // Consume 50 tokens at t=200ms.
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }

  // Over 200ms at 100 tokens/s, 20 tokens refill: token count goes from 50 to 70.
  // Average tokens = (50 + 70) / 2 = 60. Consumed tokens = 100 - 60 = 40.
  // Area = 0.2s * 40 tokens = 8.0 token-seconds.
  // Utilization = 8.0 / (0.2s * 100 max_tokens) = 0.40 (40%).
  time_system.advanceTimeWait(std::chrono::milliseconds(200));
  EXPECT_NEAR(0.40, bucket.popBucketUtilization(), 1e-4);

  // At t=200ms, token count was 70. popBucketUtilization reset accumulated
  // area. Over the next 500ms, 30 tokens refill in 300ms (average 85 tokens,
  // missing 15 tokens for 0.3s -> area 4.5 token-seconds). Then the bucket is
  // full (100 tokens) for the remaining 200ms (0 consumed area). Total
  // utilization = 4.5 / (0.5s * 100) = 0.09 (9%).
  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_NEAR(0.09, bucket.popBucketUtilization(), 1e-4);

  // Once full, subsequent idle period has 0.0 utilization.
  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());
}

TEST(DynamicTokenBucketTest, ScaleRefillRateAdjustsCapacityAndRefillRate) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  // Scale up by 2.0 -> rate 200, max tokens 200, tokens 200, pressure 0.0.
  bucket.scaleRefillRate(2.0);
  EXPECT_DOUBLE_EQ(200.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(200.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(200.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);

  // Scale down by 0.5 -> rate 100, max tokens 100, tokens 100, pressure 0.0.
  bucket.scaleRefillRate(0.5);
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(100.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(100.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);
}

TEST(DynamicTokenBucketTest, ScaleRefillRatePreservesConsumedFraction) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  // Consume 40 tokens -> 60 tokens remain, pressure 0.40.
  for (int i = 0; i < 40; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(60.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.40, bucket.getResourceUsage().resource_pressure_);

  // Scale up by 2.0 -> rate 200, max tokens 200, tokens 120, pressure 0.40.
  bucket.scaleRefillRate(2.0);
  EXPECT_DOUBLE_EQ(200.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(200.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(120.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.40, bucket.getResourceUsage().resource_pressure_);

  // Scale down by 0.5 -> rate 100, max tokens 100, tokens 60, pressure 0.40.
  bucket.scaleRefillRate(0.5);
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(100.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(60.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.40, bucket.getResourceUsage().resource_pressure_);
}

TEST(DynamicTokenBucketTest, SetRefillRateDirectlyUpdatesRateAndCapacity) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));

  // Directly set rate to 50.0, then update in place to 80.0.
  bucket.setRefillRate(50.0);
  EXPECT_DOUBLE_EQ(50.0, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(50.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(50.0, bucket.tokenCount());

  bucket.setRefillRate(80.0);
  EXPECT_DOUBLE_EQ(80.0, bucket.refillRate());
  EXPECT_DOUBLE_EQ(80.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(80.0, bucket.tokenCount());

  // Non-positive or non-finite rates do nothing.
  bucket.setRefillRate(0.0);
  EXPECT_DOUBLE_EQ(80.0, bucket.refillRate());
  bucket.setRefillRate(-10.0);
  EXPECT_DOUBLE_EQ(80.0, bucket.refillRate());
  bucket.setRefillRate(std::numeric_limits<double>::quiet_NaN());
  EXPECT_DOUBLE_EQ(80.0, bucket.refillRate());
  bucket.setRefillRate(std::numeric_limits<double>::infinity());
  EXPECT_DOUBLE_EQ(80.0, bucket.refillRate());
  bucket.setRefillRate(-std::numeric_limits<double>::infinity());
  EXPECT_DOUBLE_EQ(80.0, bucket.refillRate());
}

TEST(DynamicTokenBucketTest, PopTokenConsumptionRateComputesRateAndResets) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));

  // Initially idle -> 0 consumption rate.
  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_DOUBLE_EQ(0.0, bucket.popTokenConsumptionRate());

  // Consume 50 tokens over 500ms.
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }
  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  // 50 tokens / 0.5s = 100 tokens/s.
  EXPECT_DOUBLE_EQ(100.0, bucket.popTokenConsumptionRate());

  // Second pop with no accepted load returns 0.0.
  time_system.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_DOUBLE_EQ(0.0, bucket.popTokenConsumptionRate());

  // Consume 30 tokens over 200ms -> 150 tokens/s.
  for (int i = 0; i < 30; ++i) {
    bucket.onLoadAccepted();
  }
  time_system.advanceTimeWait(std::chrono::milliseconds(200));
  EXPECT_NEAR(150.0, bucket.popTokenConsumptionRate(), 1e-4);
}

TEST(DynamicTokenBucketTest, SubUnitRateClampsMaxTokensToAtLeastOne) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(0.5);

  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(1.0, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(1.0, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);

  // Consuming the single token drops token_count to 0.0 (pressure 1.0).
  // Over 1s at 0.5 tokens/s, token_count refills to 0.5 (< 1.0), reporting
  // 1.0 pressure and 0.75 bucket utilization.
  bucket.onLoadAccepted();
  EXPECT_DOUBLE_EQ(1.0, bucket.getResourceUsage().resource_pressure_);
  time_system.advanceTimeWait(std::chrono::seconds(1));
  EXPECT_DOUBLE_EQ(1.0, bucket.getResourceUsage().resource_pressure_);
  EXPECT_DOUBLE_EQ(0.5, bucket.tokenCount());
  EXPECT_NEAR(0.75, bucket.popBucketUtilization(), 1e-4);
}

TEST(DynamicTokenBucketTest, MidIntervalRefillRateChangePreservesTimeWeightedUtilization) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());

  // Consume 50 tokens at t=0 -> 50 tokens remain in bucket of 100.
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }

  // Interval 1: 100ms at 100 tokens/s -> 10 tokens refill (50 -> 60, utilization = 0.45).
  time_system.advanceTimeWait(std::chrono::milliseconds(100));

  // Change refill rate mid-interval to 200 tokens/s: refills first, then scales 60 -> 120.
  bucket.setRefillRate(200.0);
  EXPECT_DOUBLE_EQ(120.0, bucket.tokenCount());

  // Interval 2: 300ms at 200 tokens/s -> 60 tokens refill (120 -> 180, average 150, missing 50,
  // utilization = 0.25). Time-weighted average over 100ms + 300ms = (0.1 * 0.45 + 0.3 * 0.25) /
  // 0.4 = 0.30.
  time_system.advanceTimeWait(std::chrono::milliseconds(300));
  EXPECT_NEAR(0.30, bucket.popBucketUtilization(), 1e-4);

  // Mid-window getResourceUsage() (+50ms: 180 -> 190, area 0.75) followed by refilling past full
  // (+250ms: 190 -> 200 in 50ms, area +0.25 = 1.0) accumulates across both refill segments.
  time_system.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_DOUBLE_EQ(0.05, bucket.getResourceUsage().resource_pressure_);
  time_system.advanceTimeWait(std::chrono::milliseconds(250));
  EXPECT_NEAR(1.0 / 60.0, bucket.popBucketUtilization(), 1e-5);
}

TEST(DynamicTokenBucketTest, SetRefillRateAfterUninitializedTimeDoesNotDiluteUtilization) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));

  // Advance time while uninitialized without calling popBucketUtilization().
  time_system.advanceTimeWait(std::chrono::seconds(1));

  // Initialize refill rate to 100 tokens/s at t=1s.
  bucket.setRefillRate(100.0);

  // Consume 50 tokens.
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }

  // Advance time by 200ms -> 20 tokens refill (50 -> 70 tokens).
  // Average tokens = 60, missing tokens = 40.
  // Area = 0.2s * 40 tokens = 8.0 token-seconds.
  // Post-initialization window duration = 0.2s.
  // Undiluted utilization = 8.0 / (0.2s * 100 max_tokens) = 0.40.
  time_system.advanceTimeWait(std::chrono::milliseconds(200));
  EXPECT_NEAR(0.40, bucket.popBucketUtilization(), 1e-4);
}

TEST(DynamicTokenBucketTest, SmallPositiveRefillRateClampedToMinRefillRate) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(1e-6);

  EXPECT_DOUBLE_EQ(1e-3, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(1.0, *bucket.maxTokens());
}

TEST(DynamicTokenBucketTest, ScaleRefillRateIgnoresNonPositiveAndNonFiniteFactors) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  bucket.scaleRefillRate(0.0);
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());

  bucket.scaleRefillRate(-2.0);
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());

  bucket.scaleRefillRate(std::numeric_limits<double>::quiet_NaN());
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());

  bucket.scaleRefillRate(std::numeric_limits<double>::infinity());
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());

  bucket.scaleRefillRate(-std::numeric_limits<double>::infinity());
  EXPECT_DOUBLE_EQ(100.0, bucket.refillRate());
}

TEST(DynamicTokenBucketTest, ZeroElapsedDurationPreservesUtilizationAndConsumptionState) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(100.0);

  // Consume 50 tokens at t=0.
  for (int i = 0; i < 50; ++i) {
    bucket.onLoadAccepted();
  }

  // Without advancing time, popBucketUtilization() and
  // popTokenConsumptionRate() return 0.0 without clearing consumed tokens.
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());
  EXPECT_DOUBLE_EQ(0.0, bucket.popTokenConsumptionRate());

  // Advance time by 200ms -> 20 tokens refill (50 -> 70 tokens).
  // Average tokens = 60, missing tokens = 40.
  // Area = 0.2s * 40 tokens = 8.0 token-seconds.
  // Utilization = 8.0 / (0.2s * 100 max_tokens) = 0.40.
  // Consumed tokens = 50 over 0.2s -> rate = 250 tokens/s.
  time_system.advanceTimeWait(std::chrono::milliseconds(200));

  EXPECT_NEAR(0.40, bucket.popBucketUtilization(), 1e-4);
  EXPECT_NEAR(250.0, bucket.popTokenConsumptionRate(), 1e-4);

  // Subsequent calls without advancing time also return 0.0.
  EXPECT_DOUBLE_EQ(0.0, bucket.popBucketUtilization());
  EXPECT_DOUBLE_EQ(0.0, bucket.popTokenConsumptionRate());
}

TEST(DynamicTokenBucketTest, LargeRefillRateAndScalingClampedWithoutOverflowOrNaN) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(10));
  bucket.setRefillRate(100.0);

  for (int i = 0; i < 500; ++i) {
    bucket.onLoadAccepted();
  }
  EXPECT_DOUBLE_EQ(500.0, bucket.tokenCount());

  // Overflowing scaleRefillRate from a sub-max rate clamps refill_rate_ to 1e12,
  // and 10s * 1e12 = 1e13 clamps max_tokens_ to kMaxTokens (1e12).
  bucket.scaleRefillRate(1e308);
  EXPECT_DOUBLE_EQ(1e12, bucket.refillRate());
  ASSERT_TRUE(bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(1e12, *bucket.maxTokens());
  EXPECT_DOUBLE_EQ(5e11, bucket.tokenCount());
  EXPECT_DOUBLE_EQ(0.5, bucket.getResourceUsage().resource_pressure_);

  // Direct setRefillRate above kMaxRefillRate when empty (0 tokens, 0 area) clamps cleanly.
  DynamicTokenBucket zero_bucket(time_system, absl::Milliseconds(10));
  zero_bucket.setRefillRate(1e-3);
  zero_bucket.onLoadAccepted();
  zero_bucket.setRefillRate(1e308);
  EXPECT_DOUBLE_EQ(1e12, zero_bucket.refillRate());
  ASSERT_TRUE(zero_bucket.maxTokens().has_value());
  EXPECT_DOUBLE_EQ(1e10, *zero_bucket.maxTokens());
  EXPECT_DOUBLE_EQ(0.0, zero_bucket.tokenCount());
  EXPECT_DOUBLE_EQ(1.0, zero_bucket.getResourceUsage().resource_pressure_);

  time_system.advanceTimeWait(std::chrono::milliseconds(10));
  EXPECT_DOUBLE_EQ(0.5, zero_bucket.popBucketUtilization());
}

TEST(DynamicTokenBucketTest, DownscalingPreservesExactFullAndSingleTokenBoundaries) {
  {
    Event::SimulatedTimeSystem time_system;
    DynamicTokenBucket bucket(time_system, absl::Seconds(1));
    bucket.setRefillRate(3.0);
    bucket.setRefillRate(1.4);

    ASSERT_TRUE(bucket.maxTokens().has_value());
    EXPECT_EQ(1.4, *bucket.maxTokens());
    EXPECT_EQ(1.4, bucket.tokenCount());
    EXPECT_DOUBLE_EQ(0.0, bucket.getResourceUsage().resource_pressure_);
  }

  {
    Event::SimulatedTimeSystem time_system;
    DynamicTokenBucket bucket(time_system, absl::Seconds(1));
    bucket.setRefillRate(98.0);
    for (int i = 0; i < 49; ++i) {
      bucket.onLoadAccepted();
    }
    ASSERT_DOUBLE_EQ(49.0, bucket.tokenCount());

    bucket.setRefillRate(2.0);
    ASSERT_TRUE(bucket.maxTokens().has_value());
    EXPECT_DOUBLE_EQ(2.0, *bucket.maxTokens());
    EXPECT_EQ(1.0, bucket.tokenCount());
    EXPECT_DOUBLE_EQ(0.5, bucket.getResourceUsage().resource_pressure_);
  }
}

TEST(DynamicTokenBucketTest, ExtremeDownscalingUnderflowClampsToMinRefillRate) {
  Event::SimulatedTimeSystem time_system;
  DynamicTokenBucket bucket(time_system, absl::Seconds(1));
  bucket.setRefillRate(0.1);

  bucket.scaleRefillRate(1e-323);
  EXPECT_DOUBLE_EQ(1e-3, bucket.refillRate());
}

} // namespace
} // namespace AdaptiveWorkerCapacity
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
