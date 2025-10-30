#include "contrib/peak_ewma/load_balancing_policies/source/peak_ewma_lb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

class CostTest : public ::testing::Test {
protected:
  Cost cost_;                                          // Uses default penalty value (1000000.0)
  static constexpr double kDefaultRtt = 10.0;          // 10ms default
  static constexpr double kDefaultPenalty = 1000000.0; // Default penalty value
};

TEST_F(CostTest, ComputesCostWithRttAndRequests) {
  // RTT available, requests active: cost = rtt * (requests + 1)
  double cost = cost_.compute(20.0, 5.0, kDefaultRtt);
  EXPECT_EQ(cost, 20.0 * (5.0 + 1.0)); // 120.0
}

TEST_F(CostTest, ComputesCostWithRttAndZeroRequests) {
  // RTT available, no requests: cost = rtt * 1
  double cost = cost_.compute(30.0, 0.0, kDefaultRtt);
  EXPECT_EQ(cost, 30.0 * 1.0); // 30.0
}

TEST_F(CostTest, ComputesCostWithoutRttButWithRequests) {
  // No RTT, but requests active: penalty + requests
  double cost = cost_.compute(0.0, 3.0, kDefaultRtt);
  EXPECT_EQ(cost, kDefaultPenalty + 3.0);
}

TEST_F(CostTest, ComputesCostWithoutRttAndZeroRequests) {
  // No RTT, no requests: use default RTT assumption
  double cost = cost_.compute(0.0, 0.0, kDefaultRtt);
  EXPECT_EQ(cost, kDefaultRtt * 1.0); // 10.0
}

TEST_F(CostTest, HandlesZeroDefaultRtt) {
  // Edge case: zero default RTT
  double cost = cost_.compute(0.0, 0.0, 0.0);
  EXPECT_EQ(cost, 0.0);
}

TEST_F(CostTest, PrefersFreshRttOverDefault) {
  // When RTT is available, ignore default
  double cost_with_rtt = cost_.compute(50.0, 2.0, kDefaultRtt);
  double expected = 50.0 * (2.0 + 1.0); // 150.0
  EXPECT_EQ(cost_with_rtt, expected);
  EXPECT_NE(cost_with_rtt, kDefaultRtt * (2.0 + 1.0)); // Should not use default
}

TEST_F(CostTest, ConfigurablePenaltyValue) {
  // Test custom penalty value
  const double custom_penalty = 500000.0;
  Cost cost_with_custom_penalty(custom_penalty);

  double cost = cost_with_custom_penalty.compute(0.0, 2.0, kDefaultRtt);
  EXPECT_EQ(cost, custom_penalty + 2.0);

  // Verify different from default penalty
  double cost_with_default = cost_.compute(0.0, 2.0, kDefaultRtt);
  EXPECT_NE(cost, cost_with_default);
  EXPECT_EQ(cost_with_default, kDefaultPenalty + 2.0);
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
