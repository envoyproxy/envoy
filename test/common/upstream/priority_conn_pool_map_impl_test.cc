#include <memory>

#include "envoy/http/conn_pool.h"

#include "source/common/upstream/priority_conn_pool_map_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

class PriorityConnPoolMapImplTest : public testing::Test {
public:
  using TestMap = PriorityConnPoolMap<int, Http::ConnectionPool::Instance>;
  using TestMapPtr = std::unique_ptr<TestMap>;

  TestMapPtr makeTestMap() { return std::make_unique<TestMap>(dispatcher_, host_); }

  TestMap::PoolFactory getBasicFactory() {
    return [&]() {
      auto pool = std::make_unique<NiceMock<Http::ConnectionPool::MockInstance>>();
      ON_CALL(*pool, hasActiveConnections).WillByDefault(Return(false));
      mock_pools_.push_back(pool.get());
      return pool;
    };
  }

  TestMap::PoolFactory getNeverCalledFactory() {
    return []() {
      EXPECT_TRUE(false);
      return nullptr;
    };
  }

protected:
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<NiceMock<Http::ConnectionPool::MockInstance>*> mock_pools_;
  std::shared_ptr<NiceMock<MockHost>> host_ = std::make_shared<NiceMock<MockHost>>();
};

// Show that we return a non-null value, and that we invoke the default resource manager
TEST_F(PriorityConnPoolMapImplTest, DefaultPriorityProxiedThrough) {
  TestMapPtr test_map = makeTestMap();

  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::High)).Times(0);
  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::Default)).Times(AtLeast(1));

  auto pool = test_map->getPool(ResourcePriority::Default, 0, getBasicFactory());
  EXPECT_TRUE(pool.has_value());

  // At this point, we may clean up/decrement by 0, etc, so allow any number.
  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::High)).Times(AtLeast(1));
}

// Show that we return a non-null value, and that we invoke the high resource manager
TEST_F(PriorityConnPoolMapImplTest, HighPriorityProxiedThrough) {
  TestMapPtr test_map = makeTestMap();

  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::Default)).Times(0);
  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::High)).Times(AtLeast(1));

  auto pool = test_map->getPool(ResourcePriority::High, 0, getBasicFactory());
  EXPECT_TRUE(pool.has_value());

  // At this point, we may clean up/decrement by 0, etc, so allow any number.
  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::Default)).Times(AtLeast(1));
}

TEST_F(PriorityConnPoolMapImplTest, TestSizeForSinglePriority) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(ResourcePriority::High, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::High, 1, getBasicFactory());

  EXPECT_EQ(test_map->size(), 2);
}

TEST_F(PriorityConnPoolMapImplTest, TestSizeForMultiplePriorities) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(ResourcePriority::High, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::High, 1, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 1, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 2, getBasicFactory());

  EXPECT_EQ(test_map->size(), 5);
}

TEST_F(PriorityConnPoolMapImplTest, TestClearEmptiesOut) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(ResourcePriority::High, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::High, 1, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 1, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 2, getBasicFactory());
  test_map->clear();

  EXPECT_TRUE(test_map->empty());
}

TEST_F(PriorityConnPoolMapImplTest, TestErase) {
  TestMapPtr test_map = makeTestMap();

  auto* pool_ptr = &test_map->getPool(ResourcePriority::High, 1, getBasicFactory()).value().get();
  EXPECT_EQ(1, test_map->size());
  EXPECT_EQ(pool_ptr,
            &test_map->getPool(ResourcePriority::High, 1, getNeverCalledFactory()).value().get());
  EXPECT_FALSE(test_map->erasePool(ResourcePriority::Default, 1));
  EXPECT_NE(pool_ptr,
            &test_map->getPool(ResourcePriority::Default, 1, getBasicFactory()).value().get());
  EXPECT_EQ(2, test_map->size());
  EXPECT_TRUE(test_map->erasePool(ResourcePriority::Default, 1));
  EXPECT_TRUE(test_map->erasePool(ResourcePriority::High, 1));
  EXPECT_TRUE(test_map->empty());
  EXPECT_NE(pool_ptr,
            &test_map->getPool(ResourcePriority::High, 1, getBasicFactory()).value().get());
}

// Show that the drained callback is invoked once for the high priority pool, and once for
// the default priority pool.
TEST_F(PriorityConnPoolMapImplTest, TestAddDrainedCbProxiedThrough) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(ResourcePriority::High, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 0, getBasicFactory());

  Http::ConnectionPool::Instance::IdleCb cbHigh;
  EXPECT_CALL(*mock_pools_[0], addIdleCallback(_)).WillOnce(SaveArg<0>(&cbHigh));
  Http::ConnectionPool::Instance::IdleCb cbDefault;
  EXPECT_CALL(*mock_pools_[1], addIdleCallback(_)).WillOnce(SaveArg<0>(&cbDefault));

  ReadyWatcher watcher;
  test_map->addIdleCallback([&watcher]() { watcher.ready(); });

  EXPECT_CALL(watcher, ready()).Times(2);
  cbHigh();
  cbDefault();
}

TEST_F(PriorityConnPoolMapImplTest, TestDrainConnectionsProxiedThrough) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(ResourcePriority::High, 0, getBasicFactory());
  test_map->getPool(ResourcePriority::Default, 0, getBasicFactory());

  EXPECT_CALL(*mock_pools_[0],
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  EXPECT_CALL(*mock_pools_[1],
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));

  test_map->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
