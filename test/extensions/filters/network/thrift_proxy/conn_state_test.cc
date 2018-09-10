#include "extensions/filters/network/thrift_proxy/conn_state.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

// Test behavior of nextSequenceId()
TEST(ThriftConnectionStateTest, NextSequenceId) {
  ThriftConnectionState cs;

  EXPECT_EQ(0, cs.nextSequenceId());
  EXPECT_EQ(1, cs.nextSequenceId());

  cs.setNextSequenceIdForTest(std::numeric_limits<int32_t>::max());

  // Wraps around without producing negative values.
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), cs.nextSequenceId());
  EXPECT_EQ(0, cs.nextSequenceId());
}

// Test how markUpgraded/upgradedAttempts/isUpgraded when upgrade is successful.
TEST(ThriftConnectionStateTest, TestUpgradeSucceeded) {
  ThriftConnectionState cs;
  EXPECT_FALSE(cs.upgradeAttempted());
  EXPECT_FALSE(cs.isUpgraded());

  cs.markUpgraded();
  EXPECT_TRUE(cs.upgradeAttempted());
  EXPECT_TRUE(cs.isUpgraded());
}

// Test how markUpgraded/upgradedAttempts/isUpgraded when upgrade fails.
TEST(ThriftConnectionStateTest, TestUpgradeFailed) {
  ThriftConnectionState cs;
  EXPECT_FALSE(cs.upgradeAttempted());
  EXPECT_FALSE(cs.isUpgraded());

  cs.markUpgradeFailed();
  EXPECT_TRUE(cs.upgradeAttempted());
  EXPECT_FALSE(cs.isUpgraded());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
