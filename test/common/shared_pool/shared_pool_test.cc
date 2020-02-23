#include <thread>

#include "common/shared_pool/shared_pool.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

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

TEST(SharedPoolTest, GetObjectAndDeleteObjectRaceForSameHashValue) {
  Event::MockDispatcher dispatcher;
  auto pool = std::make_shared<ObjectSharedPool<int>>(dispatcher);
  auto o1 = pool->getObject(4);
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  pool->sync().enable();
  pool->sync().waitOn(ObjectSharedPool<int>::DeleteObjectOnMainThread);
  EXPECT_CALL(dispatcher, post(_)).WillOnce(Invoke([pool](auto callback) {
    // Set the correct thread id, or we'll always call Dispatcher::post to deliver the task to the
    // correct thread to execute.
    pool->setThreadIdForTest(std::this_thread::get_id());
    EXPECT_EQ(1, pool->poolSize());
    callback(); // Blocks in thread synchronizer waiting on DeleteObjectOnMainThread
  }));

  auto thread = thread_factory.createThread([&o1]() {
    // simulation of shared objects destructing in other threads
    o1.reset();
  });
  pool->sync().barrierOn(ObjectSharedPool<int>::DeleteObjectOnMainThread);

  // The deleteObject method has not been executed yet, when it is switched to the main thread and
  // called getObject again to get an object with the same hash value.
  pool->setThreadIdForTest(std::this_thread::get_id());
  auto o2 = pool->getObject(4);

  pool->sync().signal(ObjectSharedPool<int>::DeleteObjectOnMainThread);
  thread->join();
  // deleteObject will to release older weak_ptr objects
  // Because the storage is actually a new weak_ptr and the reference count is not zero, it is not
  // deleted
  EXPECT_EQ(1, pool->poolSize());
}

TEST(SharedPoolTest, RaceCondtionForGetObjectWithObjectDeleter) {
  Event::MockDispatcher dispatcher;
  auto pool = std::make_shared<ObjectSharedPool<int>>(dispatcher);
  auto o1 = pool->getObject(4);

  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  pool->sync().enable();
  pool->sync().waitOn(ObjectSharedPool<int>::ObjectDeleterEntry);
  EXPECT_CALL(dispatcher, post(_)).WillOnce(Invoke([pool](auto) {
    // Overrides the default behavior, do nothing
  }));
  auto thread = thread_factory.createThread([&o1]() {
    // simulation of shared objects destructing in other threads
    o1.reset();
  });
  pool->sync().barrierOn(ObjectSharedPool<int>::ObjectDeleterEntry);

  pool->setThreadIdForTest(std::this_thread::get_id());
  // Object is destructing, no memory has been released,
  // at this time the object obtained through getObject is newly created, not the old object,
  // so the object memory is released when the destruct is complete, and o2 is still valid.
  auto o2 = pool->getObject(4);
  pool->sync().signal(ObjectSharedPool<int>::ObjectDeleterEntry);
  thread->join();
  EXPECT_EQ(4, *o2);
}

} // namespace SharedPool
} // namespace Envoy
