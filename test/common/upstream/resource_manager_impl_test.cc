#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/upstream/resource_manager_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Upstream {

TEST(ResourceManagerImplTest, RuntimeResourceManager) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockGauge> gauge;
  NiceMock<Stats::MockStore> store;

  ON_CALL(store, gauge(_)).WillByDefault(ReturnRef(gauge));

  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 0, 0, 0, 1,
      ClusterCircuitBreakersStats{ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_GAUGE(store))});

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

} // namespace Upstream
} // namespace Envoy
