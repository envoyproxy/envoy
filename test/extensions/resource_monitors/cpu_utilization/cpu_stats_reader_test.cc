#include "source/extensions/resource_monitors/cpu_utilization/cpu_stats_reader.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

// =============================================================================
// CpuTimes Struct Construction Tests
// =============================================================================

TEST(CpuTimesTest, DefaultConstruction) {
  CpuTimes times{true, false, 100.0, 1000, 2.0};
  EXPECT_TRUE(times.is_valid);
  EXPECT_FALSE(times.is_cgroup_v2);
  EXPECT_EQ(times.work_time, 100.0);
  EXPECT_EQ(times.total_time, 1000);
  EXPECT_EQ(times.effective_cores, 2.0);
}

TEST(CpuTimesTest, InvalidCpuTimes) {
  CpuTimes times{false, false, 0.0, 0, 0.0};
  EXPECT_FALSE(times.is_valid);
}

// =============================================================================
// Cgroup V1 calculateUtilization() Tests
// =============================================================================

TEST(CpuTimesV1Test, BasicUtilizationCalculation) {
  // Previous: work=100, total=1000
  // Current:  work=200, total=2000
  // Delta:    work=100, total=1000
  // Utilization = 100/1000 = 0.1 (10%)
  CpuTimes previous{true, false, 100.0, 1000, 1.0};
  CpuTimes current{true, false, 200.0, 2000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.1);
}

TEST(CpuTimesV1Test, FullUtilization) {
  // Work equals total time = 100% utilization
  CpuTimes previous{true, false, 0.0, 0, 1.0};
  CpuTimes current{true, false, 1000.0, 1000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesV1Test, ZeroUtilization) {
  // No work done
  CpuTimes previous{true, false, 0.0, 0, 1.0};
  CpuTimes current{true, false, 0.0, 1000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.0);
}

TEST(CpuTimesV1Test, HighUtilization) {
  // 95% utilization
  CpuTimes previous{true, false, 0.0, 0, 1.0};
  CpuTimes current{true, false, 950.0, 1000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.95);
}

TEST(CpuTimesV1Test, LargeTimeValues) {
  // Test with large time values (typical for long-running systems)
  CpuTimes previous{true, false, 1000000.0, 10000000, 1.0};
  CpuTimes current{true, false, 2000000.0, 20000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.1);
}

TEST(CpuTimesV1Test, SmallTimeIncrement) {
  // Test with very small time increments
  CpuTimes previous{true, false, 100.0, 1000, 1.0};
  CpuTimes current{true, false, 100.1, 1001, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.1, 0.001);
}

TEST(CpuTimesV1Test, FractionalWorkTime) {
  // Test with fractional work times
  CpuTimes previous{true, false, 123.456, 1000, 1.0};
  CpuTimes current{true, false, 234.567, 2000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.111111, 0.0001);
}

// =============================================================================
// Cgroup V2 calculateUtilization() Tests
// =============================================================================

TEST(CpuTimesV2Test, BasicUtilizationCalculation) {
  // V2: work_time in microseconds, total_time in nanoseconds
  // Previous: work=0, total=0
  // Current:  work=500000 μs (0.5s), total=1000000000 ns (1s), cores=1
  // Utilization = (500000/1000000) / (1000000000/1000000000 * 1) = 0.5 / 1.0 = 0.5
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 500000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesV2Test, FullUtilization) {
  // Full utilization on 1 core
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 1000000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesV2Test, ZeroUtilization) {
  // No work done
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 0.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.0);
}

TEST(CpuTimesV2Test, MultipleEffectiveCores) {
  // 2 cores, full utilization would be 2000000 μs in 1s
  // Using 1000000 μs = 50% of 2 cores = 0.5 utilization
  CpuTimes previous{true, true, 0.0, 0, 2.0};
  CpuTimes current{true, true, 1000000.0, 1000000000, 2.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesV2Test, FourEffectiveCores) {
  // 4 cores, using 2000000 μs in 1s = 2 full cores = 0.5 utilization
  CpuTimes previous{true, true, 0.0, 0, 4.0};
  CpuTimes current{true, true, 2000000.0, 1000000000, 4.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesV2Test, FractionalEffectiveCores) {
  // 0.5 cores (throttled container)
  // Using 250000 μs in 1s with 0.5 cores = 0.5 utilization
  CpuTimes previous{true, true, 0.0, 0, 0.5};
  CpuTimes current{true, true, 250000.0, 1000000000, 0.5};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesV2Test, HighUtilization) {
  // 95% utilization
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 950000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.95);
}

TEST(CpuTimesV2Test, ClampingAtMaximum) {
  // Value exceeds 1.0 and should be clamped
  // Using 1500000 μs in 1s on 1 core would be 1.5, clamped to 1.0
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 1500000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesV2Test, ClampingAtMinimum) {
  // Negative values should be clamped to 0.0 (though validation would catch negatives)
  // This tests the clamping logic itself with a value very close to 0
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 1.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.0, 0.000001);
  EXPECT_GE(utilization, 0.0);
}

TEST(CpuTimesV2Test, LargeTimeValues) {
  // Test with large time values
  CpuTimes previous{true, true, 1000000000.0, 1000000000000, 2.0};
  CpuTimes current{true, true, 2000000000.0, 2000000000000, 2.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesV2Test, SmallTimeIncrement) {
  // Test with very small time increments
  // Delta: work=100 μs, total=100000000 ns (0.1s), 1 core
  // Calculation: (100/1000000) / (0.1 * 1) = 0.0001 / 0.1 = 0.001
  CpuTimes previous{true, true, 100000.0, 1000000000, 1.0};
  CpuTimes current{true, true, 100100.0, 1100000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.001, 0.0001);
}

// =============================================================================
// Error Handling Tests - Negative work_over_period
// =============================================================================

TEST(CpuTimesErrorTest, NegativeWorkOverPeriodV1) {
  // Current work_time is less than previous (clock regression or reset)
  CpuTimes previous{true, false, 1000.0, 1000, 1.0};
  CpuTimes current{true, false, 500.0, 2000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, NegativeWorkOverPeriodV2) {
  CpuTimes previous{true, true, 1000000.0, 1000000000, 1.0};
  CpuTimes current{true, true, 500000.0, 2000000000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

// =============================================================================
// Error Handling Tests - Zero or Negative total_over_period
// =============================================================================

TEST(CpuTimesErrorTest, ZeroTotalOverPeriodV1) {
  // Current total_time equals previous (no time passed)
  CpuTimes previous{true, false, 100.0, 1000, 1.0};
  CpuTimes current{true, false, 200.0, 1000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, ZeroTotalOverPeriodV2) {
  CpuTimes previous{true, true, 100000.0, 1000000000, 1.0};
  CpuTimes current{true, true, 200000.0, 1000000000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, NegativeTotalOverPeriodV1) {
  // Current total_time is less than previous (clock regression)
  CpuTimes previous{true, false, 100.0, 2000, 1.0};
  CpuTimes current{true, false, 200.0, 1000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, NegativeTotalOverPeriodV2) {
  CpuTimes previous{true, true, 100000.0, 2000000000, 1.0};
  CpuTimes current{true, true, 200000.0, 1000000000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, BothNegativeDeltas) {
  // Both work and total are negative (extreme clock regression)
  CpuTimes previous{true, false, 2000.0, 3000, 1.0};
  CpuTimes current{true, false, 1000.0, 1000, 1.0};

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, NegativeWorkWithPositiveTotal) {
  // Test specifically: work_over_period < 0 AND total_over_period > 0
  // This ensures the first branch of the OR condition is covered independently
  CpuTimes previous{true, false, 1000.0, 1000, 1.0};
  CpuTimes current{true, false, 500.0, 2000, 1.0}; // work decreased, total increased

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, PositiveWorkWithNonPositiveTotal) {
  // Test specifically: work_over_period >= 0 AND total_over_period <= 0
  // This ensures the second branch of the OR condition is covered independently
  CpuTimes previous{true, false, 100.0, 2000, 1.0};
  CpuTimes current{true, false, 200.0, 1000, 1.0}; // work increased, total decreased

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

TEST(CpuTimesErrorTest, PositiveWorkWithZeroTotal) {
  // Test specifically: work_over_period > 0 AND total_over_period == 0
  // Ensures zero total is caught even when work is positive
  CpuTimes previous{true, false, 100.0, 1000, 1.0};
  CpuTimes current{true, false, 200.0, 1000, 1.0}; // work increased, total same (delta=0)

  double utilization;
  absl::Status status = current.calculateUtilization(previous, utilization);
  EXPECT_FALSE(status.ok());
}

// =============================================================================
// Boundary Value Tests for Clamping
// =============================================================================

TEST(CpuTimesV2Test, ValueExactlyAtLowerBound) {
  // Test value exactly at 0.0 (no clamping needed at lower bound)
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 0.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.0);
}

TEST(CpuTimesV2Test, ValueExactlyAtUpperBound) {
  // Test value exactly at 1.0 (no clamping needed at upper bound)
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 1000000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesV2Test, ValueJustBelowUpperBound) {
  // Test value just below 1.0 (0.999999)
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 999999.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.999999, 0.000001);
  EXPECT_LT(utilization, 1.0);
}

TEST(CpuTimesV2Test, ValueJustAboveZero) {
  // Test very small positive value (just above 0.0)
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 0.001, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_GT(utilization, 0.0);
  EXPECT_LT(utilization, 0.000001);
}

TEST(CpuTimesV2Test, ValueWellAboveUpperBound) {
  // Test value well above 1.0 (should clamp to 1.0)
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 10000000.0, 1000000000, 1.0}; // Would be 10.0 without clamping

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesV1Test, BoundaryValueZero) {
  // Test V1 with zero utilization (boundary)
  CpuTimes previous{true, false, 1000.0, 2000, 1.0};
  CpuTimes current{true, false, 1000.0, 3000, 1.0}; // No work done

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.0);
}

TEST(CpuTimesV1Test, BoundaryValueOne) {
  // Test V1 with 100% utilization (boundary)
  CpuTimes previous{true, false, 0.0, 0, 1.0};
  CpuTimes current{true, false, 1000.0, 1000, 1.0}; // All time is work

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesV1Test, ValueAboveOne) {
  // Test V1 with value > 1.0 (V1 doesn't clamp, so this is valid)
  CpuTimes previous{true, false, 0.0, 0, 1.0};
  CpuTimes current{true, false, 2000.0, 1000,
                   1.0}; // Work exceeds total (shouldn't happen in practice)

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 2.0);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST(CpuTimesEdgeCaseTest, VerySmallUtilizationV1) {
  // 0.001% utilization
  CpuTimes previous{true, false, 0.0, 0, 1.0};
  CpuTimes current{true, false, 1.0, 100000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.00001, 0.000001);
}

TEST(CpuTimesEdgeCaseTest, VerySmallUtilizationV2) {
  // 0.001% utilization on 1 core
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 10.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.00001, 0.000001);
}

TEST(CpuTimesEdgeCaseTest, ManyEffectiveCores) {
  // Test with 16 cores
  CpuTimes previous{true, true, 0.0, 0, 16.0};
  CpuTimes current{true, true, 8000000.0, 1000000000, 16.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesEdgeCaseTest, SingleMillicore) {
  // Extremely limited CPU allocation (0.001 cores)
  CpuTimes previous{true, true, 0.0, 0, 0.001};
  CpuTimes current{true, true, 500.0, 1000000000, 0.001};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesEdgeCaseTest, VeryHighWorkOnMultipleCoresV2) {
  // Using 3 full cores out of 4 available
  CpuTimes previous{true, true, 0.0, 0, 4.0};
  CpuTimes current{true, true, 3000000.0, 1000000000, 4.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.75);
}

TEST(CpuTimesEdgeCaseTest, MaxedOutSingleCore) {
  // Single core completely maxed out
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 999999.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_NEAR(utilization, 0.999999, 0.000001);
}

// =============================================================================
// Unit Conversion Tests (V2-specific)
// =============================================================================

TEST(CpuTimesUnitConversionTest, MicrosecondsToSecondsConversion) {
  // Verify microseconds to seconds conversion: 1000000 μs = 1 s
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 1000000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 1.0);
}

TEST(CpuTimesUnitConversionTest, NanosecondsToSecondsConversion) {
  // Verify nanoseconds to seconds conversion: 1000000000 ns = 1 s
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 500000.0, 1000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesUnitConversionTest, DifferentTimeScales) {
  // Work: 0.5 seconds = 500000 μs
  // Total: 2 seconds = 2000000000 ns
  // 1 core: utilization = 0.5s / 2s = 0.25
  CpuTimes previous{true, true, 0.0, 0, 1.0};
  CpuTimes current{true, true, 500000.0, 2000000000, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.25);
}

// =============================================================================
// Comparison Tests (V1 vs V2 behavior)
// =============================================================================

TEST(CpuTimesComparisonTest, V1AndV2DifferentCalculations) {
  // Same raw values but different cgroup versions produce different results
  // This demonstrates the different calculation formulas

  // V1: simple ratio (work/total)
  CpuTimes prev_v1{true, false, 0.0, 0, 1.0};
  CpuTimes curr_v1{true, false, 500.0, 1000, 1.0};
  double util_v1;

  absl::Status status_v1 = curr_v1.calculateUtilization(prev_v1, util_v1);

  ASSERT_TRUE(status_v1.ok());
  EXPECT_DOUBLE_EQ(util_v1, 0.5);

  // V2: unit conversion + core accounting
  // If these were v2 values, interpretation would be different
  CpuTimes prev_v2{true, true, 0.0, 0, 1.0};
  CpuTimes curr_v2{true, true, 500.0, 1000, 1.0};
  double util_v2;

  absl::Status status_v2 = curr_v2.calculateUtilization(prev_v2, util_v2);

  ASSERT_TRUE(status_v2.ok());

  // V2 calculation: (500/1000000) / ((1000/1000000000) * 1) = 0.0005 / 0.000001 = 500
  // Then clamped to 1.0
  EXPECT_DOUBLE_EQ(util_v2, 1.0);
}

// =============================================================================
// Stress Tests with Extreme Values
// =============================================================================

TEST(CpuTimesStressTest, MaxUint64TotalTime) {
  // Test with very large uint64 value (approaching max)
  uint64_t large_time = 18446744073709551000ULL; // Near UINT64_MAX
  CpuTimes previous{true, false, 0.0, large_time - 10000, 1.0};
  CpuTimes current{true, false, 5000.0, large_time, 1.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

TEST(CpuTimesStressTest, VeryLongRunningSystem) {
  // Simulate a system running for a very long time (days/weeks)
  // Uptime: ~30 days = 2592000000000000 nanoseconds
  uint64_t base_time = 2592000000000000ULL;
  CpuTimes previous{true, true, 100000000.0, base_time, 2.0};
  CpuTimes current{true, true, 101000000.0, base_time + 1000000000, 2.0};

  double utilization;

  absl::Status status = current.calculateUtilization(previous, utilization);

  ASSERT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(utilization, 0.5);
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
