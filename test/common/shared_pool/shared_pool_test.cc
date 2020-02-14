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
  std::shared_ptr<ObjectSharedPool<int>> pool;

  std::thread another_thread(
      [&pool, &dispatcher] { pool = std::make_shared<ObjectSharedPool<int>>(dispatcher); });
  another_thread.join();
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

  // different threads

  std::thread another_thread(
      [&pool, &dispatcher] { pool.reset(new ObjectSharedPool<int>(dispatcher)); });
  another_thread.join();
  EXPECT_CALL(dispatcher, post(_)).WillOnce(Invoke([](auto) {
    // Overrides the default behavior, do nothing
  }));
  pool->deleteObject(std::hash<int>{}(4));
}

TEST(SharedPoolTest, NonThreadSafeForPoolSizeDeathTest) {
  Event::MockDispatcher dispatcher;
  std::shared_ptr<ObjectSharedPool<int>> pool;
  std::thread another_thread(
      [&pool, &dispatcher] { pool = std::make_shared<ObjectSharedPool<int>>(dispatcher); });
  another_thread.join();
  EXPECT_DEBUG_DEATH(pool->poolSize(), ".*");
}

} // namespace SharedPool
} // namespace Envoy
