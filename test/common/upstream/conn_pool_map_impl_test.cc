#include <memory>
#include <vector>

#include "envoy/http/conn_pool.h"

#include "source/common/upstream/conn_pool_map_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

class ConnPoolMapImplTest : public testing::Test {
public:
  // Note, we could test with Http::ConnectionPool::MockInstance here, which would simplify the
  // test. However, it's nice to test against an actual interface we'll be using.
  using TestMap = ConnPoolMap<int, Http::ConnectionPool::Instance>;
  using TestMapPtr = std::unique_ptr<TestMap>;

  TestMapPtr makeTestMap() {
    return std::make_unique<TestMap>(dispatcher_, host_, ResourcePriority::Default);
  }

  TestMapPtr makeTestMapWithLimit(uint64_t limit) {
    return makeTestMapWithLimitAtPriority(limit, ResourcePriority::Default);
  }

  TestMapPtr makeTestMapWithLimitAtPriority(uint64_t limit, ResourcePriority priority) {
    host_->cluster_.resetResourceManager(1024, 1024, 1024, 1024, limit, 100);
    return std::make_unique<TestMap>(dispatcher_, host_, priority);
  }

  TestMap::PoolFactory getBasicFactory() {
    return [&]() {
      auto pool = std::make_unique<NiceMock<Http::ConnectionPool::MockInstance>>();
      ON_CALL(*pool, hasActiveConnections).WillByDefault(Return(false));
      mock_pools_.push_back(pool.get());
      return pool;
    };
  }

  // Returns a pool which claims it has active connections.
  TestMap::PoolFactory getActivePoolFactory() {
    return [&]() {
      auto pool = std::make_unique<NiceMock<Http::ConnectionPool::MockInstance>>();
      ON_CALL(*pool, hasActiveConnections).WillByDefault(Return(true));
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

  TestMap::PoolFactory getFactoryExpectIdleCb(Http::ConnectionPool::Instance::IdleCb* cb) {
    return [this, cb]() {
      auto pool = std::make_unique<NiceMock<Http::ConnectionPool::MockInstance>>();
      EXPECT_CALL(*pool, addIdleCallback(_)).WillOnce(SaveArg<0>(cb));
      mock_pools_.push_back(pool.get());
      return pool;
    };
  }

protected:
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<NiceMock<Http::ConnectionPool::MockInstance>*> mock_pools_;
  std::shared_ptr<NiceMock<MockHost>> host_ = std::make_shared<NiceMock<MockHost>>();
};

TEST_F(ConnPoolMapImplTest, TestMapIsEmptyOnConstruction) {
  TestMapPtr test_map = makeTestMap();

  EXPECT_EQ(test_map->size(), 0);
}

TEST_F(ConnPoolMapImplTest, TestAddingAConnPoolIncreasesSize) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  EXPECT_EQ(test_map->size(), 1);
}

TEST_F(ConnPoolMapImplTest, TestAddingTwoConnPoolsIncreasesSize) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  EXPECT_EQ(test_map->size(), 2);
}

TEST_F(ConnPoolMapImplTest, TestConnPoolReturnedMatchesCreated) {
  TestMapPtr test_map = makeTestMap();

  TestMap::PoolOptRef pool = test_map->getPool(1, getBasicFactory());
  EXPECT_EQ(&(pool.value().get()), mock_pools_[0]);
}

TEST_F(ConnPoolMapImplTest, TestConnSecondPoolReturnedMatchesCreated) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  TestMap::PoolOptRef pool = test_map->getPool(2, getBasicFactory());
  EXPECT_EQ(&(pool.value().get()), mock_pools_[1]);
}

TEST_F(ConnPoolMapImplTest, TestMultipleOfSameKeyReturnsOriginal) {
  TestMapPtr test_map = makeTestMap();

  TestMap::PoolOptRef pool1 = test_map->getPool(1, getBasicFactory());
  TestMap::PoolOptRef pool2 = test_map->getPool(2, getBasicFactory());

  EXPECT_EQ(&(pool1.value().get()), &(test_map->getPool(1, getBasicFactory()).value().get()));
  EXPECT_EQ(&(pool2.value().get()), &(test_map->getPool(2, getBasicFactory()).value().get()));
  EXPECT_EQ(test_map->size(), 2);
}

TEST_F(ConnPoolMapImplTest, TestEmptyClerWorks) {
  TestMapPtr test_map = makeTestMap();

  test_map->clear();
  EXPECT_EQ(test_map->size(), 0);
}

TEST_F(ConnPoolMapImplTest, TestClearEmptiesOutMap) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());

  test_map->clear();
  EXPECT_EQ(test_map->size(), 0);
}

TEST_F(ConnPoolMapImplTest, CallbacksPassedToPools) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  Http::ConnectionPool::Instance::IdleCb cb1;
  EXPECT_CALL(*mock_pools_[0], addIdleCallback(_)).WillOnce(SaveArg<0>(&cb1));
  Http::ConnectionPool::Instance::IdleCb cb2;
  EXPECT_CALL(*mock_pools_[1], addIdleCallback(_)).WillOnce(SaveArg<0>(&cb2));

  ReadyWatcher watcher;
  test_map->addIdleCallback([&watcher]() { watcher.ready(); });
  test_map->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

  EXPECT_CALL(watcher, ready()).Times(2);
  cb1();
  cb2();
}

// Tests that if we add the callback first, it is passed along when pools are created later.
TEST_F(ConnPoolMapImplTest, CallbacksCachedAndPassedOnCreation) {
  TestMapPtr test_map = makeTestMap();

  ReadyWatcher watcher;
  test_map->addIdleCallback([&watcher]() { watcher.ready(); });
  test_map->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

  Http::ConnectionPool::Instance::IdleCb cb1;
  test_map->getPool(1, getFactoryExpectIdleCb(&cb1));

  Http::ConnectionPool::Instance::IdleCb cb2;
  test_map->getPool(2, getFactoryExpectIdleCb(&cb2));

  EXPECT_CALL(watcher, ready()).Times(2);
  cb1();
  cb2();
}

// Tests that if we drain connections on an empty map, nothing happens.
TEST_F(ConnPoolMapImplTest, EmptyMapDrainConnectionsNop) {
  TestMapPtr test_map = makeTestMap();
  test_map->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
}

// Tests that we forward drainConnections to the pools.
TEST_F(ConnPoolMapImplTest, DrainConnectionsForwarded) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  EXPECT_CALL(*mock_pools_[0],
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  EXPECT_CALL(*mock_pools_[1],
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));

  test_map->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
}

TEST_F(ConnPoolMapImplTest, ClearDefersDelete) {
  TestMapPtr test_map = makeTestMap();

  Http::ConnectionPool::Instance::IdleCb cb1;
  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  test_map->clear();

  EXPECT_EQ(dispatcher_.to_delete_.size(), 2);
}

TEST_F(ConnPoolMapImplTest, GetPoolHittingLimitFails) {
  TestMapPtr test_map = makeTestMapWithLimit(1);

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], hasActiveConnections()).WillByDefault(Return(true));
  auto opt_pool = test_map->getPool(2, getNeverCalledFactory());

  EXPECT_FALSE(opt_pool.has_value());
  EXPECT_EQ(test_map->size(), 1);
}

TEST_F(ConnPoolMapImplTest, GetPoolHittingLimitIncrementsFailureCounter) {
  TestMapPtr test_map = makeTestMapWithLimit(1);

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], hasActiveConnections()).WillByDefault(Return(true));
  test_map->getPool(2, getNeverCalledFactory());

  EXPECT_EQ(host_->cluster_.traffic_stats_->upstream_cx_pool_overflow_.value(), 1);
}

TEST_F(ConnPoolMapImplTest, GetPoolHittingLimitIncrementsFailureMultiple) {
  TestMapPtr test_map = makeTestMapWithLimit(1);

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], hasActiveConnections()).WillByDefault(Return(true));
  test_map->getPool(2, getNeverCalledFactory());
  test_map->getPool(2, getNeverCalledFactory());
  test_map->getPool(2, getNeverCalledFactory());

  EXPECT_EQ(host_->cluster_.traffic_stats_->upstream_cx_pool_overflow_.value(), 3);
}

TEST_F(ConnPoolMapImplTest, GetPoolHittingLimitGreaterThan1Fails) {
  TestMapPtr test_map = makeTestMapWithLimit(2);

  test_map->getPool(1, getActivePoolFactory());
  test_map->getPool(2, getActivePoolFactory());
  auto opt_pool = test_map->getPool(3, getNeverCalledFactory());

  EXPECT_FALSE(opt_pool.has_value());
  EXPECT_EQ(test_map->size(), 2);
}

TEST_F(ConnPoolMapImplTest, GetPoolLimitHitThenOneFreesUpNextCallSucceeds) {
  TestMapPtr test_map = makeTestMapWithLimit(1);

  test_map->getPool(1, getActivePoolFactory());
  test_map->getPool(2, getNeverCalledFactory());

  ON_CALL(*mock_pools_[0], hasActiveConnections()).WillByDefault(Return(false));

  auto opt_pool = test_map->getPool(2, getBasicFactory());

  EXPECT_TRUE(opt_pool.has_value());
  EXPECT_EQ(test_map->size(), 1);
}

TEST_F(ConnPoolMapImplTest, GetPoolLimitHitFollowedBySuccessDoesNotClearFailure) {
  TestMapPtr test_map = makeTestMapWithLimit(1);

  test_map->getPool(1, getActivePoolFactory());
  test_map->getPool(2, getNeverCalledFactory());

  ON_CALL(*mock_pools_[0], hasActiveConnections()).WillByDefault(Return(false));

  test_map->getPool(2, getBasicFactory());
  EXPECT_EQ(host_->cluster_.traffic_stats_->upstream_cx_pool_overflow_.value(), 1);
}

// Test that only the pool which are idle are actually cleared
TEST_F(ConnPoolMapImplTest, GetOnePoolIdleOnlyClearsThatOne) {
  TestMapPtr test_map = makeTestMapWithLimit(2);

  // Get a pool which says it's not active.
  test_map->getPool(1, getBasicFactory());

  // Get one that *is* active.
  auto opt_pool = test_map->getPool(2, getActivePoolFactory());

  // this should force out #1
  auto new_pool = test_map->getPool(3, getBasicFactory());

  // Get 2 again. It should succeed, but not invoke the factory.
  auto opt_pool2 = test_map->getPool(2, getNeverCalledFactory());

  EXPECT_TRUE(opt_pool.has_value());
  EXPECT_TRUE(new_pool.has_value());
  EXPECT_EQ(&(opt_pool.value().get()), &(opt_pool2.value().get()));
  EXPECT_EQ(test_map->size(), 2);
}

// Show that even if all pools are idle, we only free up one as necessary
TEST_F(ConnPoolMapImplTest, GetPoolLimitHitManyIdleOnlyOneFreed) {
  TestMapPtr test_map = makeTestMapWithLimit(3);

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  test_map->getPool(3, getBasicFactory());
  auto opt_pool = test_map->getPool(4, getBasicFactory());

  ASSERT_TRUE(opt_pool.has_value());
  EXPECT_EQ(test_map->size(), 3);
}

// Show that if we hit the limit once, then again with the same keys, we don't clean out the
// previously cleaned entries. Essentially, ensure we clean up any state related to being full.
TEST_F(ConnPoolMapImplTest, GetPoolFailStateIsCleared) {
  TestMapPtr test_map = makeTestMapWithLimit(2);

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getActivePoolFactory());
  test_map->getPool(3, getBasicFactory());

  // At this point, 1 should be cleared out. Let's get it again, then trigger a full condition.
  auto opt_pool = test_map->getPool(1, getActivePoolFactory());
  EXPECT_TRUE(opt_pool.has_value());

  // We're full. Because pool 1  and 2 are busy, the next call should fail.
  auto opt_pool_failed = test_map->getPool(4, getNeverCalledFactory());
  EXPECT_FALSE(opt_pool_failed.has_value());

  EXPECT_EQ(test_map->size(), 2);
}

TEST_F(ConnPoolMapImplTest, CircuitBreakerNotSetOnClear) {
  TestMapPtr test_map = makeTestMapWithLimit(1);

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  test_map->getPool(3, getBasicFactory());

  test_map->clear();

  EXPECT_EQ(host_->cluster_.circuit_breakers_stats_.cx_pool_open_.value(), 0);
}

TEST_F(ConnPoolMapImplTest, CircuitBreakerSetAtLimit) {
  TestMapPtr test_map = makeTestMapWithLimit(2);

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());

  EXPECT_EQ(host_->cluster_.circuit_breakers_stats_.cx_pool_open_.value(), 1);
}

TEST_F(ConnPoolMapImplTest, CircuitBreakerClearedOnDestroy) {
  {
    TestMapPtr test_map = makeTestMapWithLimit(2);

    test_map->getPool(1, getBasicFactory());
    test_map->getPool(2, getBasicFactory());
  }

  EXPECT_EQ(host_->cluster_.circuit_breakers_stats_.cx_pool_open_.value(), 0);
}

TEST_F(ConnPoolMapImplTest, CircuitBreakerUsesProvidedPriorityDefault) {
  TestMapPtr test_map = makeTestMapWithLimitAtPriority(2, ResourcePriority::Default);

  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::High)).Times(0);
  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::Default)).Times(AtLeast(1));

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
}

TEST_F(ConnPoolMapImplTest, CircuitBreakerUsesProvidedPriorityHigh) {
  TestMapPtr test_map = makeTestMapWithLimitAtPriority(2, ResourcePriority::High);

  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::High)).Times(AtLeast(1));
  EXPECT_CALL(host_->cluster_, resourceManager(ResourcePriority::Default)).Times(0);

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
}

TEST_F(ConnPoolMapImplTest, ErasePool) {
  TestMapPtr test_map = makeTestMap();
  auto* pool_ptr = &test_map->getPool(1, getBasicFactory()).value().get();
  EXPECT_EQ(1, test_map->size());
  EXPECT_EQ(pool_ptr, &test_map->getPool(1, getNeverCalledFactory()).value().get());
  EXPECT_EQ(1, test_map->size());
  EXPECT_FALSE(test_map->erasePool(2));
  EXPECT_EQ(1, test_map->size());
  EXPECT_TRUE(test_map->erasePool(1));
  EXPECT_EQ(0, test_map->size());
  EXPECT_NE(pool_ptr, &test_map->getPool(1, getBasicFactory()).value().get());
}

// The following tests only die in debug builds, so don't run them if this isn't one.
#if !defined(NDEBUG)
class ConnPoolMapImplDeathTest : public ConnPoolMapImplTest {};

TEST_F(ConnPoolMapImplDeathTest, ReentryClearTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addIdleCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::IdleCb cb) { cb(); }));

  EXPECT_DEATH(test_map->addIdleCallback([&test_map]() { test_map->clear(); }),
               ".*Details: A resource should only be entered once");
}

TEST_F(ConnPoolMapImplDeathTest, ReentryGetPoolTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addIdleCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::IdleCb cb) { cb(); }));

  EXPECT_DEATH(
      test_map->addIdleCallback([&test_map, this]() { test_map->getPool(2, getBasicFactory()); }),
      ".*Details: A resource should only be entered once");
}

TEST_F(ConnPoolMapImplDeathTest, ReentryDrainConnectionsTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addIdleCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::IdleCb cb) { cb(); }));

  EXPECT_DEATH(test_map->addIdleCallback([&test_map]() { test_map->clear(); }),
               ".*Details: A resource should only be entered once");
}

TEST_F(ConnPoolMapImplDeathTest, ReentryAddDrainedCallbackTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addIdleCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::IdleCb cb) { cb(); }));

  EXPECT_DEATH(test_map->addIdleCallback([&test_map]() { test_map->addIdleCallback([]() {}); }),
               ".*Details: A resource should only be entered once");
}
#endif // !defined(NDEBUG)

} // namespace
} // namespace Upstream
} // namespace Envoy
