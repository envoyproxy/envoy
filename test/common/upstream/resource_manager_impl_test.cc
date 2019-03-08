#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/upstream/resource_manager_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

TEST(ResourceManagerImplTest, RuntimeResourceManager) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockBoolIndicator> bool_indicator;
  NiceMock<Stats::MockGauge> gauge;
  NiceMock<Stats::MockStore> store;

  ON_CALL(store, boolIndicator(_)).WillByDefault(ReturnRef(bool_indicator));
  ON_CALL(store, gauge(_)).WillByDefault(ReturnRef(gauge));

  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 0, 0, 0, 1,
      ClusterCircuitBreakersStats{ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_BOOL_INDICATOR(store),
                                                                     POOL_GAUGE(store))});

  EXPECT_CALL(
      runtime.snapshot_,
      getInteger("circuit_breakers.runtime_resource_manager_test.default.max_connections", 0U))
      .Times(2)
      .WillRepeatedly(Return(1U));
  EXPECT_EQ(1U, resource_manager.connections().max());
  EXPECT_TRUE(resource_manager.connections().canCreate());

  EXPECT_CALL(
      runtime.snapshot_,
      getInteger("circuit_breakers.runtime_resource_manager_test.default.max_pending_requests", 0U))
      .Times(2)
      .WillRepeatedly(Return(2U));
  EXPECT_EQ(2U, resource_manager.pendingRequests().max());
  EXPECT_TRUE(resource_manager.pendingRequests().canCreate());

  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.runtime_resource_manager_test.default.max_requests", 0U))
      .Times(2)
      .WillRepeatedly(Return(3U));
  EXPECT_EQ(3U, resource_manager.requests().max());
  EXPECT_TRUE(resource_manager.requests().canCreate());

  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.runtime_resource_manager_test.default.max_retries", 1U))
      .Times(2)
      .WillRepeatedly(Return(0U));
  EXPECT_EQ(0U, resource_manager.retries().max());
  EXPECT_FALSE(resource_manager.retries().canCreate());
}

TEST(ResourceManagerImplTest, RemainingResourceGauges) {
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl store;

  auto stats = ClusterCircuitBreakersStats{ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_BOOL_INDICATOR(store),
                                           POOL_GAUGE(store))};
  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 1, 2, 1, 0,
      stats);
  
  // Test remaining_cx_ gauge
  EXPECT_EQ(1U, resource_manager.connections().max());
  EXPECT_EQ(1U, stats.remaining_cx_.value());
  resource_manager.connections().inc();
  EXPECT_EQ(0U, stats.remaining_cx_.value());
  resource_manager.connections().dec();
  EXPECT_EQ(1U, stats.remaining_cx_.value());

  // Test remaining_pending_ gauge
  EXPECT_EQ(2U, resource_manager.pendingRequests().max());
  EXPECT_EQ(2U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().inc();
  EXPECT_EQ(1U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().inc();
  EXPECT_EQ(0U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().dec();
  EXPECT_EQ(1U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().dec();
  EXPECT_EQ(2U, stats.remaining_pending_.value());

  // Test remaining_rq_ gauge
  EXPECT_EQ(1U, resource_manager.requests().max());
  EXPECT_EQ(1U, stats.remaining_rq_.value());
  resource_manager.requests().inc();
  EXPECT_EQ(0U, stats.remaining_rq_.value());
  resource_manager.requests().dec();
  EXPECT_EQ(1U, stats.remaining_rq_.value());

  // Test remaining_retries_ gauge. Confirm that the value will not be negative
  // despite having more retries than the configured max
  EXPECT_EQ(0U, resource_manager.retries().max());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  resource_manager.retries().inc();
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  resource_manager.retries().dec();
  EXPECT_EQ(0U, stats.remaining_retries_.value());
}
} // namespace
} // namespace Upstream
} // namespace Envoy
