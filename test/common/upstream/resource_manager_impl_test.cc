#include "common/upstream/resource_manager_impl.h"

#include "test/mocks/runtime/mocks.h"

using testing::NiceMock;
using testing::Return;

namespace Upstream {

TEST(ResourceManagerImplTest, RuntimeResourceManager) {
  NiceMock<Runtime::MockLoader> runtime;
  ResourceManagerImpl resource_manager(
      runtime, "circuit_breakers.runtime_resource_manager_test.default.", 0, 0, 0, 0);

  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.runtime_resource_manager_test.default.max_connections",
                         0U)).WillOnce(Return(1U));
  EXPECT_EQ(1U, resource_manager.connections().max());

  EXPECT_CALL(
      runtime.snapshot_,
      getInteger("circuit_breakers.runtime_resource_manager_test.default.max_pending_requests", 0U))
      .WillOnce(Return(2U));
  EXPECT_EQ(2U, resource_manager.pendingRequests().max());

  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.runtime_resource_manager_test.default.max_requests", 0U))
      .WillOnce(Return(3U));
  EXPECT_EQ(3U, resource_manager.requests().max());

  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.runtime_resource_manager_test.default.max_retries", 0U))
      .WillOnce(Return(4U));
  EXPECT_EQ(4U, resource_manager.retries().max());
}
}
