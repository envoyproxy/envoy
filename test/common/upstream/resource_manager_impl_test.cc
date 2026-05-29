#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "source/common/upstream/resource_manager_impl.h"

#include "test/mocks/event/mocks.h"
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

ClusterCircuitBreakersStats clusterCircuitBreakersStats(Stats::Store& store) {
  return {
      ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(c, POOL_GAUGE(store), h, tr, GENERATE_STATNAME_STRUCT)};
}

TEST(ResourceManagerImplTest, RuntimeResourceManager) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockGauge> gauge;
  NiceMock<Stats::MockStore> store;
  NiceMock<Event::MockDispatcher> dispatcher;

  ON_CALL(store, gauge(_, _)).WillByDefault(ReturnRef(gauge));

  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 0, 0, 0, 1, 0, 100,
      clusterCircuitBreakersStats(store), absl::nullopt, absl::nullopt, absl::nullopt, dispatcher);

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
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.runtime_resource_manager_test."
                                            "default.retry_budget.budget_interval",
                                            _))
      .WillRepeatedly(Return(100U));
  EXPECT_EQ(5U, resource_manager.retries().max());
  EXPECT_TRUE(resource_manager.retries().canCreate());
}

TEST(ResourceManagerImplTest, RemainingResourceGauges) {
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl store;
  NiceMock<Event::MockDispatcher> dispatcher;

  auto stats = clusterCircuitBreakersStats(store);
  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 1, 2, 1, 0, 3, 100, stats,
      absl::nullopt, absl::nullopt, absl::nullopt, dispatcher);

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
  NiceMock<Event::MockDispatcher> dispatcher;

  auto stats = clusterCircuitBreakersStats(store);

  // Test retry budgets disable remaining_retries gauge (it should always be 0).
  ResourceManagerImpl rm(runtime, "circuit_breakers.runtime_resource_manager_test.default.", 1, 2,
                         1, 0, 3, 100, stats, 20.0, static_cast<uint64_t>(100),
                         static_cast<uint32_t>(5), dispatcher);

  EXPECT_EQ(5U, rm.retries().max());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  EXPECT_EQ(0U, rm.retries().count());
  rm.retries().inc();
  EXPECT_EQ(1U, rm.retries().count());
  EXPECT_EQ(0U, stats.remaining_retries_.value());
  EXPECT_EQ(100u, rm.maxConnectionsPerHost());
  rm.retries().dec();
}

TEST(ResourceManagerImplTest, RetryBudgetIntervalDisabled) {
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl store;
  NiceMock<Event::MockDispatcher> dispatcher;

  auto stats = clusterCircuitBreakersStats(store);

  // budget_interval=0 means use in-flight request count, no sliding window.
  ResourceManagerImpl rm(runtime, "circuit_breakers.runtime_resource_manager_test.default.", 1024,
                         0, 1024, 0, 3, 100, stats, 20.0, static_cast<uint64_t>(0),
                         static_cast<uint32_t>(5), dispatcher);

  // Expect max retries to be the min_retry_concurrency.
  EXPECT_EQ(5U, rm.retries().max());

  // Add 100 in-flight requests.
  for (int i = 0; i < 100; i++) {
    rm.requests().inc();
  }
  // max retries = 20% * 100 = 20.
  EXPECT_EQ(20U, rm.retries().max());

  // Completing 50 requests; reducing in-flight count.
  for (int i = 0; i < 50; i++) {
    rm.requests().dec();
  }
  // max retries = 20% * 50 = 10.
  EXPECT_EQ(10U, rm.retries().max());

  // Add 50 pending requests. Budget uses requests + pending as in-flight count.
  for (int i = 0; i < 50; i++) {
    rm.pendingRequests().inc();
  }
  // max retries = 20% * 100 = 20.
  EXPECT_EQ(20U, rm.retries().max());

  // Completing all remaining requests.
  for (int i = 0; i < 50; i++) {
    rm.pendingRequests().dec();
    rm.requests().dec();
  }
  // Expect max retries to be the min_retry_concurrency.
  EXPECT_EQ(5U, rm.retries().max());

  rm.retries().inc();
  EXPECT_EQ(1U, rm.retries().count());
  EXPECT_TRUE(rm.retries().canCreate());

  for (int i = 0; i <= 5; i++) {
    rm.retries().inc();
  }
  EXPECT_FALSE(rm.retries().canCreate());
}

TEST(ResourceManagerImplTest, RetryBudgetIntervalEnabled) {
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl store;
  NiceMock<Event::MockDispatcher> dispatcher;
  Event::MockTimer* main_timer = new Event::MockTimer(&dispatcher);

  auto stats = clusterCircuitBreakersStats(store);

  // budget_interval=100ms, expiration_interval = 100ms/10 = 10ms.
  EXPECT_CALL(*main_timer, enableTimer(std::chrono::milliseconds(10), _))
      .Times(testing::AnyNumber());
  ResourceManagerImpl rm(runtime, "circuit_breakers.runtime_resource_manager_test.default.", 1024,
                         0, 1024, 0, 3, 100, stats, 20.0, static_cast<uint64_t>(100),
                         static_cast<uint32_t>(5), dispatcher);

  EXPECT_EQ(5U, rm.retries().max());
  for (int i = 0; i < 100; i++) {
    rm.requests().inc();
  }
  EXPECT_EQ(20U, rm.retries().max());

  // first callback adds the 100 requests into the deque.
  main_timer->invokeCallback();

  // Add 50 more requests after resetting req_to_expire_.
  for (int i = 0; i < 50; i++) {
    rm.requests().inc();
  }
  // max retries = 20% * 150 = 30.
  EXPECT_EQ(30U, rm.retries().max());

  // Invoke the callback 8 times, so the deque has all 9 slots.
  for (int i = 0; i < 8; i++) {
    main_timer->invokeCallback();
  }
  EXPECT_EQ(30U, rm.retries().max());

  // 100 requests are expired from the oldest slot of the deque / subtracted from req_in_interval_.
  main_timer->invokeCallback();

  // req_in_interval_ now holds 50 requests.
  // max retries = 20% * 50 = 10.
  EXPECT_EQ(10U, rm.retries().max());

  // Expire last 50 requests
  main_timer->invokeCallback();

  // All requests expired. Add 10 new ones.
  for (int i = 0; i < 10; i++) {
    rm.requests().inc();
  }
  // max retries = 20% * 10 = 2. Use min retry concurrency (5).
  EXPECT_EQ(5U, rm.retries().max());

  rm.retries().inc();
  EXPECT_EQ(1U, rm.retries().count());
  EXPECT_TRUE(rm.retries().canCreate());

  for (int i = 0; i <= 5; i++) {
    rm.retries().inc();
  }
  EXPECT_FALSE(rm.retries().canCreate());
}
} // namespace
} // namespace Upstream
} // namespace Envoy
