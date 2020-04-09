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
  NiceMock<Stats::MockGauge> gauge;
  NiceMock<Stats::MockStore> store;

  ON_CALL(store, gauge(_, _)).WillByDefault(ReturnRef(gauge));

  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 0, 0, 0, 1, 0,
      ClusterCircuitBreakersStats{
          ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_GAUGE(store), POOL_GAUGE(store))},
      absl::nullopt, absl::nullopt);

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

  EXPECT_CALL(
      runtime.snapshot_,
      getInteger("circuit_breakers.runtime_resource_manager_test.default.max_connection_pools", 0U))
      .Times(2)
      .WillRepeatedly(Return(5U));
  EXPECT_EQ(5U, resource_manager.connectionPools().max());
  EXPECT_TRUE(resource_manager.connectionPools().canCreate());

  // Verify retry budgets override max_retries.
  std::string value;
  EXPECT_CALL(runtime.snapshot_, get(_)).WillRepeatedly(Return(value));
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.runtime_resource_manager_test."
                                            "default.retry_budget.min_retry_concurrency",
                                            _))
      .WillRepeatedly(Return(5U));
  EXPECT_EQ(5U, resource_manager.retries().max());
  EXPECT_TRUE(resource_manager.retries().canCreate());
}

TEST(ResourceManagerImplTest, RemainingResourceGauges) {
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl store;

  auto stats = ClusterCircuitBreakersStats{
      ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_GAUGE(store), POOL_GAUGE(store))};
  ResourceManagerImpl resource_manager(runtime,
                                       "circuit_breakers.runtime_resource_manager_test.default.", 1,
                                       2, 1, 0, 3, stats, absl::nullopt, absl::nullopt);

  // Test remaining_cx_ gauge
  EXPECT_EQ(1U, resource_manager.connections().max());
  EXPECT_EQ(1U, stats.remaining_cx_.value());
  EXPECT_EQ(0U, resource_manager.connections().count());
  resource_manager.connections().inc();
  EXPECT_EQ(1U, resource_manager.connections().count());
  EXPECT_EQ(0U, stats.remaining_cx_.value());
  resource_manager.connections().dec();
  EXPECT_EQ(1U, stats.remaining_cx_.value());

  // Test remaining_pending_ gauge
  EXPECT_EQ(2U, resource_manager.pendingRequests().max());
  EXPECT_EQ(2U, stats.remaining_pending_.value());
  EXPECT_EQ(0U, resource_manager.pendingRequests().count());
  resource_manager.pendingRequests().inc();
  EXPECT_EQ(1U, resource_manager.pendingRequests().count());
  EXPECT_EQ(1U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().inc();
  EXPECT_EQ(0U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().dec();
  EXPECT_EQ(1U, stats.remaining_pending_.value());
  resource_manager.pendingRequests().dec();
  EXPECT_EQ(2U, stats.remaining_pending_.value());
  EXPECT_EQ(2U, stats.remaining_pending_.value());

  // Test remaining_rq_ gauge
  EXPECT_EQ(1U, resource_manager.requests().max());
  EXPECT_EQ(1U, stats.remaining_rq_.value());
  EXPECT_EQ(0U, resource_manager.requests().count());
  resource_manager.requests().inc();
  EXPECT_EQ(1U, resource_manager.requests().count());
  EXPECT_EQ(0U, stats.remaining_rq_.value());
  resource_manager.requests().dec();
  EXPECT_EQ(1U, stats.remaining_rq_.value());

  // Test remaining_retries_ gauge. Confirm that the value will not be negative
  // despite having more retries than the configured max
  EXPECT_EQ(0U, resource_manager.retries().max());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  EXPECT_EQ(0U, resource_manager.retries().count());
  resource_manager.retries().inc();
  EXPECT_EQ(1U, resource_manager.retries().count());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  resource_manager.retries().dec();

  // Test remaining_cx_pools gauge.
  EXPECT_EQ(3U, resource_manager.connectionPools().max());
  EXPECT_EQ(3U, stats.remaining_cx_pools_.value());
  EXPECT_EQ(0U, resource_manager.connectionPools().count());
  resource_manager.connectionPools().inc();
  EXPECT_EQ(1U, resource_manager.connectionPools().count());
  EXPECT_EQ(2U, stats.remaining_cx_pools_.value());
  resource_manager.connectionPools().dec();
  EXPECT_EQ(3U, stats.remaining_cx_pools_.value());
}

TEST(ResourceManagerImplTest, RetryBudgetOverrideGauge) {
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl store;

  auto stats = ClusterCircuitBreakersStats{
      ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_GAUGE(store), POOL_GAUGE(store))};

  // Test retry budgets disable remaining_retries gauge (it should always be 0).
  ResourceManagerImpl rm(runtime, "circuit_breakers.runtime_resource_manager_test.default.", 1, 2,
                         1, 0, 3, stats, 20.0, 5);

  EXPECT_EQ(5U, rm.retries().max());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  EXPECT_EQ(0U, rm.retries().count());
  rm.retries().inc();
  EXPECT_EQ(1U, rm.retries().count());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  rm.retries().dec();
}
} // namespace
} // namespace Upstream
} // namespace Envoy
