#include <memory>
#include <vector>

#include "envoy/http/conn_pool.h"

#include "common/upstream/conn_pool_map_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {

class ConnPoolMapImplTest : public TestBase {
public:
  // Note, we could test with Http::ConnectionPool::MockInstance here, which would simplify the
  // test. However, it's nice to test against an actual interface we'll be using.
  using TestMap = ConnPoolMap<int, Http::ConnectionPool::Instance>;
  using TestMapPtr = std::unique_ptr<TestMap>;

  TestMapPtr makeTestMap() { return std::make_unique<TestMap>(dispatcher_); }

  TestMap::PoolFactory getBasicFactory() {
    return [&]() {
      auto pool = std::make_unique<NiceMock<Http::ConnectionPool::MockInstance>>();
      mock_pools_.push_back(pool.get());
      return pool;
    };
  }

  TestMap::PoolFactory getFactoryExpectDrainedCb(Http::ConnectionPool::Instance::DrainedCb* cb) {
    return [this, cb]() {
      auto pool = std::make_unique<NiceMock<Http::ConnectionPool::MockInstance>>();
      EXPECT_CALL(*pool, addDrainedCallback(_)).WillOnce(SaveArg<0>(cb));
      mock_pools_.push_back(pool.get());
      return pool;
    };
  }

protected:
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<NiceMock<Http::ConnectionPool::MockInstance>*> mock_pools_;
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

  Http::ConnectionPool::Instance& pool = test_map->getPool(1, getBasicFactory());
  EXPECT_EQ(&pool, mock_pools_[0]);
}

TEST_F(ConnPoolMapImplTest, TestConnSecondPoolReturnedMatchesCreated) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  Http::ConnectionPool::Instance& pool = test_map->getPool(2, getBasicFactory());
  EXPECT_EQ(&pool, mock_pools_[1]);
}

TEST_F(ConnPoolMapImplTest, TestMultipleOfSameKeyReturnsOriginal) {
  TestMapPtr test_map = makeTestMap();

  Http::ConnectionPool::Instance& pool1 = test_map->getPool(1, getBasicFactory());
  Http::ConnectionPool::Instance& pool2 = test_map->getPool(2, getBasicFactory());

  EXPECT_EQ(&pool1, &test_map->getPool(1, getBasicFactory()));
  EXPECT_EQ(&pool2, &test_map->getPool(2, getBasicFactory()));
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
  Http::ConnectionPool::Instance::DrainedCb cb1;
  EXPECT_CALL(*mock_pools_[0], addDrainedCallback(_)).WillOnce(SaveArg<0>(&cb1));
  Http::ConnectionPool::Instance::DrainedCb cb2;
  EXPECT_CALL(*mock_pools_[1], addDrainedCallback(_)).WillOnce(SaveArg<0>(&cb2));

  ReadyWatcher watcher;
  test_map->addDrainedCallback([&watcher] { watcher.ready(); });

  EXPECT_CALL(watcher, ready()).Times(2);
  cb1();
  cb2();
}

// Tests that if we add the callback first, it is passed along when pools are created later.
TEST_F(ConnPoolMapImplTest, CallbacksCachedAndPassedOnCreation) {
  TestMapPtr test_map = makeTestMap();

  ReadyWatcher watcher;
  test_map->addDrainedCallback([&watcher] { watcher.ready(); });

  Http::ConnectionPool::Instance::DrainedCb cb1;
  test_map->getPool(1, getFactoryExpectDrainedCb(&cb1));

  Http::ConnectionPool::Instance::DrainedCb cb2;
  test_map->getPool(2, getFactoryExpectDrainedCb(&cb2));

  EXPECT_CALL(watcher, ready()).Times(2);
  cb1();
  cb2();
}

// Tests that if we drain connections on an empty map, nothing happens.
TEST_F(ConnPoolMapImplTest, EmptyMapDrainConnectionsNop) {
  TestMapPtr test_map = makeTestMap();
  test_map->drainConnections();
}

// Tests that we forward drainConnections to the pools.
TEST_F(ConnPoolMapImplTest, DrainConnectionsForwarded) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  EXPECT_CALL(*mock_pools_[0], drainConnections());
  EXPECT_CALL(*mock_pools_[1], drainConnections());

  test_map->drainConnections();
}

TEST_F(ConnPoolMapImplTest, ClearDefersDelete) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  test_map->getPool(2, getBasicFactory());
  test_map->clear();

  EXPECT_EQ(dispatcher_.to_delete_.size(), 2);
}

// The following tests only die in debug builds, so don't run them if this isn't one.
#if !defined(NDEBUG)
class ConnPoolMapImplDeathTest : public ConnPoolMapImplTest {};

TEST_F(ConnPoolMapImplDeathTest, ReentryClearTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addDrainedCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::DrainedCb cb) { cb(); }));

  EXPECT_DEATH_LOG_TO_STDERR(test_map->addDrainedCallback([&test_map] { test_map->clear(); }),
                             ".*Details: A resource should only be entered once");
}

TEST_F(ConnPoolMapImplDeathTest, ReentryGetPoolTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addDrainedCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::DrainedCb cb) { cb(); }));

  EXPECT_DEATH_LOG_TO_STDERR(
      test_map->addDrainedCallback([&test_map, this] { test_map->getPool(2, getBasicFactory()); }),
      ".*Details: A resource should only be entered once");
}

TEST_F(ConnPoolMapImplDeathTest, ReentryDrainConnectionsTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addDrainedCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::DrainedCb cb) { cb(); }));

  EXPECT_DEATH_LOG_TO_STDERR(
      test_map->addDrainedCallback([&test_map] { test_map->drainConnections(); }),
      ".*Details: A resource should only be entered once");
}

TEST_F(ConnPoolMapImplDeathTest, ReentryAddDrainedCallbackTripsAssert) {
  TestMapPtr test_map = makeTestMap();

  test_map->getPool(1, getBasicFactory());
  ON_CALL(*mock_pools_[0], addDrainedCallback(_))
      .WillByDefault(Invoke([](Http::ConnectionPool::Instance::DrainedCb cb) { cb(); }));

  EXPECT_DEATH_LOG_TO_STDERR(
      test_map->addDrainedCallback([&test_map] { test_map->addDrainedCallback([]() {}); }),
      ".*Details: A resource should only be entered once");
}
#endif // !defined(NDEBUG)
} // namespace Upstream
} // namespace Envoy
