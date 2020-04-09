#include <thread>

#include "common/event/timer_impl.h"
#include "common/shared_pool/shared_pool.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace SharedPool {

class SharedPoolTest : public testing::Test {
protected:
  SharedPoolTest() : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()) {
    dispatcher_thread_ = api_->threadFactory().createThread([this]() {
      // Must create a keepalive timer to keep the dispatcher from exiting.
      std::chrono::milliseconds time_interval(500);
      keepalive_timer_ = dispatcher_->createTimer(
          [this, time_interval]() { keepalive_timer_->enableTimer(time_interval); });
      keepalive_timer_->enableTimer(time_interval);
      dispatcher_->run(Event::Dispatcher::RunType::Block);
    });
  }

  ~SharedPoolTest() override {
    dispatcher_->exit();
    dispatcher_thread_->join();
  }

  void deferredDeleteSharedPoolOnMainThread(std::shared_ptr<ObjectSharedPool<int>>& pool) {
    absl::Notification go;
    dispatcher_->post([&pool, &go]() {
      pool.reset();
      go.Notify();
    });
    go.WaitForNotification();
  }

  void createObjectSharedPool(std::shared_ptr<ObjectSharedPool<int>>& pool) {
    absl::Notification go;
    dispatcher_->post([&pool, &go, this]() {
      pool = std::make_shared<ObjectSharedPool<int>>(*dispatcher_);
      go.Notify();
    });
    go.WaitForNotification();
  }

  void getObjectFromObjectSharedPool(std::shared_ptr<ObjectSharedPool<int>>& pool,
                                     std::shared_ptr<int>& o, int value) {
    absl::Notification go;
    dispatcher_->post([&pool, &o, &go, value]() {
      o = pool->getObject(value);
      go.Notify();
    });
    go.WaitForNotification();
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Thread::ThreadPtr dispatcher_thread_;
  Event::TimerPtr keepalive_timer_;
  absl::Notification go_;
};

TEST_F(SharedPoolTest, Basic) {
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

TEST_F(SharedPoolTest, NonThreadSafeForGetObjectDeathTest) {
  std::shared_ptr<ObjectSharedPool<int>> pool;
  createObjectSharedPool(pool);
  EXPECT_DEBUG_DEATH(pool->getObject(4), ".*");
}

TEST_F(SharedPoolTest, ThreadSafeForDeleteObject) {
  std::shared_ptr<ObjectSharedPool<int>> pool;
  {
    // same thread
    createObjectSharedPool(pool);
    dispatcher_->post([&pool, this]() {
      pool->deleteObject(std::hash<int>{}(4));
      go_.Notify();
    });
    go_.WaitForNotification();
  }

  {
    // different threads
    createObjectSharedPool(pool);
    Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
    auto thread =
        thread_factory.createThread([&pool]() { pool->deleteObject(std::hash<int>{}(4)); });
    thread->join();
  }
}

TEST_F(SharedPoolTest, NonThreadSafeForPoolSizeDeathTest) {
  std::shared_ptr<ObjectSharedPool<int>> pool;
  createObjectSharedPool(pool);
  EXPECT_DEBUG_DEATH(pool->poolSize(), ".*");
}

TEST_F(SharedPoolTest, GetObjectAndDeleteObjectRaceForSameHashValue) {
  std::shared_ptr<ObjectSharedPool<int>> pool;
  std::shared_ptr<int> o1;
  createObjectSharedPool(pool);
  getObjectFromObjectSharedPool(pool, o1, 4);
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  pool->sync().enable();
  pool->sync().waitOn(ObjectSharedPool<int>::DeleteObjectOnMainThread);
  auto thread = thread_factory.createThread([&o1]() {
    // simulation of shared objects destructing in other threads
    o1.reset(); // Blocks in thread synchronizer waiting on DeleteObjectOnMainThread
  });
  pool->sync().barrierOn(ObjectSharedPool<int>::DeleteObjectOnMainThread);
  // The deleteObject method has not been executed yet, when it is switched to the main thread and
  // called getObject again to get an object with the same hash value.
  std::shared_ptr<int> o2;
  getObjectFromObjectSharedPool(pool, o2, 4);
  pool->sync().signal(ObjectSharedPool<int>::DeleteObjectOnMainThread);
  thread->join();

  // deleteObject will to release older weak_ptr objects
  // Because the storage is actually a new weak_ptr and the reference count is not zero, it is not
  // deleted
  dispatcher_->post([&pool, this]() {
    EXPECT_EQ(1, pool->poolSize());
    go_.Notify();
  });
  go_.WaitForNotification();
  deferredDeleteSharedPoolOnMainThread(pool);
}

TEST_F(SharedPoolTest, RaceCondtionForGetObjectWithObjectDeleter) {
  std::shared_ptr<ObjectSharedPool<int>> pool;
  std::shared_ptr<int> o1;
  createObjectSharedPool(pool);
  getObjectFromObjectSharedPool(pool, o1, 4);
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  pool->sync().enable();
  pool->sync().waitOn(ObjectSharedPool<int>::ObjectDeleterEntry);
  auto thread = thread_factory.createThread([&o1]() {
    // simulation of shared objects destructing in other threads
    o1.reset(); // Blocks in thread synchronizer waiting on ObjectDeleterEntry
  });
  pool->sync().barrierOn(ObjectSharedPool<int>::ObjectDeleterEntry);

  // Object is destructing, no memory has been released,
  // at this time the object obtained through getObject is newly created, not the old object,
  // so the object memory is released when the destruct is complete, and o2 is still valid.
  std::shared_ptr<int> o2;
  getObjectFromObjectSharedPool(pool, o2, 4);
  pool->sync().signal(ObjectSharedPool<int>::ObjectDeleterEntry);
  thread->join();
  EXPECT_EQ(4, *o2);
  deferredDeleteSharedPoolOnMainThread(pool);
}

} // namespace SharedPool
} // namespace Envoy
