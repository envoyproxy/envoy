#include <thread>

#include "common/shared_pool/shared_pool.h"

#include "test/mocks/event/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace SharedPool {

TEST(SharedPoolTest, Basic) {
  Event::MockDispatcher dispatcher;
  auto pool = std::make_shared<ObjectSharedPool<int>>(dispatcher);
  {
    auto o = pool->getObject(4);
    auto o1 = pool->getObject(4);
    ASSERT_EQ(1, pool->poolSize());

    auto o2 = pool->getObject(5);
    ASSERT_EQ(o.get(), o1.get());
    ASSERT_EQ(2, pool->poolSize());
    ASSERT_TRUE(o.get() != o2.get());
  }

  ASSERT_EQ(0, pool->poolSize());
}

TEST(SharedPoolTest, NonThreadSafeForGetObjectDeathTest) {
  Event::MockDispatcher dispatcher;
  auto pool = std::make_shared<ObjectSharedPool<int>>(dispatcher);
  pool->setThreadIdForTest(std::thread::id());
  // This section of code is executed only when release build, because getObject will trigger ASSERT
  // call at debug build causing the entire program to end, and getObject can run normally and
  // trigger object destruct to cause the deleteObject method to be executed at release build.
#ifdef NDEBUG
  EXPECT_CALL(dispatcher, post(_)).WillOnce(Invoke([&pool](auto callback) {
    // Overrides the default behavior
    // Restore thread ids so that objects can be destructed, avoiding memory leaks
    pool->setThreadIdForTest(std::this_thread::get_id());
    callback();
  }));
#endif
  EXPECT_DEBUG_DEATH(pool->getObject(4), ".*");
}

TEST(SharedPoolTest, ThreadSafeForDeleteObject) {
  Event::MockDispatcher dispatcher;
  std::shared_ptr<ObjectSharedPool<int>> pool;
  {
    // same thread
    pool.reset(new ObjectSharedPool<int>(dispatcher));
    EXPECT_CALL(dispatcher, post(_)).Times(0);
    pool->deleteObject(std::hash<int>{}(4));
  }

  {
    // different threads
    pool.reset(new ObjectSharedPool<int>(dispatcher));
    pool->setThreadIdForTest(std::thread::id());
    EXPECT_CALL(dispatcher, post(_)).WillOnce(Invoke([](auto) {
      // Overrides the default behavior, do nothing
    }));
    pool->deleteObject(std::hash<int>{}(4));
  }
}

TEST(SharedPoolTest, NonThreadSafeForPoolSizeDeathTest) {
  Event::MockDispatcher dispatcher;
  auto pool = std::make_shared<ObjectSharedPool<int>>(dispatcher);
  pool->setThreadIdForTest(std::thread::id());
  EXPECT_DEBUG_DEATH(pool->poolSize(), ".*");
}

} // namespace SharedPool
} // namespace Envoy
