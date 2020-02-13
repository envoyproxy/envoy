#include <thread>

#include "common/shared_pool/shared_pool.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(SharedPoolTest, Basic) {
  auto pool = std::make_shared<ObjectSharedPool<int>>();
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
  std::shared_ptr<ObjectSharedPool<int>> pool;

  std::thread another_thread([&pool] { pool = std::make_shared<ObjectSharedPool<int>>(); });
  another_thread.join();
  EXPECT_DEBUG_DEATH(pool->getObject(4), ".*");
}

TEST(SharedPoolTest, NonThreadSafeForDeleteObjectDeathTest) {
  std::shared_ptr<ObjectSharedPool<int>> pool;

  std::thread another_thread([&pool] { pool = std::make_shared<ObjectSharedPool<int>>(); });
  another_thread.join();
  EXPECT_DEBUG_DEATH(pool->deleteObject(std::hash<int>{}(4)), ".*");
}

TEST(SharedPoolTest, NonThreadSafeForPoolSizeDeathTest) {
  std::shared_ptr<ObjectSharedPool<int>> pool;

  std::thread another_thread([&pool] { pool = std::make_shared<ObjectSharedPool<int>>(); });
  another_thread.join();
  EXPECT_DEBUG_DEATH(pool->poolSize(), ".*");
}

} // namespace Envoy