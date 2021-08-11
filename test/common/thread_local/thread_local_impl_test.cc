#include "source/common/common/thread.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"

using testing::_;
using testing::InSequence;
using testing::Ref;
using testing::ReturnPointee;

namespace Envoy {
namespace ThreadLocal {

TEST(MainThreadVerificationTest, All) {
  // Before threading is on, assertion on main thread should be true.
  EXPECT_TRUE(Thread::MainThread::isMainThread());
  EXPECT_TRUE(Thread::MainThread::isWorkerThread());
  {
    InstanceImpl tls;
    // Tls instance has been initialized.
    // Call to main thread verification should succeed in main thread.
    EXPECT_TRUE(Thread::MainThread::isMainThread());
    EXPECT_FALSE(Thread::MainThread::isWorkerThread());
    tls.shutdownGlobalThreading();
    tls.shutdownThread();
  }
  // After threading is off, assertion on main thread should be true.
  EXPECT_TRUE(Thread::MainThread::isMainThread());
}

class TestThreadLocalObject : public ThreadLocalObject {
public:
  ~TestThreadLocalObject() override { onDestroy(); }

  MOCK_METHOD(void, onDestroy, ());
};

class ThreadLocalInstanceImplTest : public testing::Test {
public:
  ThreadLocalInstanceImplTest() {
    tls_.registerThread(main_dispatcher_, true);
    EXPECT_EQ(&main_dispatcher_, &tls_.dispatcher());
    EXPECT_CALL(thread_dispatcher_, post(_));
    tls_.registerThread(thread_dispatcher_, false);
  }

  MOCK_METHOD(ThreadLocalObjectSharedPtr, createThreadLocal, (Event::Dispatcher & dispatcher));

  TestThreadLocalObject& setObject(TypedSlot<>& slot) {
    std::shared_ptr<TestThreadLocalObject> object(new TestThreadLocalObject());
    TestThreadLocalObject& object_ref = *object;
    EXPECT_CALL(thread_dispatcher_, post(_));
    EXPECT_CALL(*this, createThreadLocal(Ref(thread_dispatcher_))).WillOnce(ReturnPointee(&object));
    EXPECT_CALL(*this, createThreadLocal(Ref(main_dispatcher_))).WillOnce(ReturnPointee(&object));
    slot.set([this](Event::Dispatcher& dispatcher) -> ThreadLocalObjectSharedPtr {
      return createThreadLocal(dispatcher);
    });
    object.reset();
    return object_ref;
  }
  int freeSlotIndexesListSize() { return tls_.free_slot_indexes_.size(); }
  InstanceImpl tls_;

  Event::MockDispatcher main_dispatcher_{"test_main_thread"};
  Event::MockDispatcher thread_dispatcher_{"test_worker_thread"};
};

TEST_F(ThreadLocalInstanceImplTest, All) {
  InSequence s;

  // Free a slot without ever calling set.
  EXPECT_CALL(thread_dispatcher_, post(_));
  TypedSlotPtr<> slot1 = TypedSlot<>::makeUnique(tls_);
  slot1.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 1);

  // Create a new slot which should take the place of the old slot. ReturnPointee() is used to
  // avoid "leaks" when using InSequence and shared_ptr.
  TypedSlotPtr<> slot2 = TypedSlot<>::makeUnique(tls_);
  TestThreadLocalObject& object_ref2 = setObject(*slot2);
  EXPECT_EQ(freeSlotIndexesListSize(), 0);

  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(object_ref2, onDestroy());
  EXPECT_EQ(freeSlotIndexesListSize(), 0);
  slot2.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 1);

  // Make two new slots, shutdown global threading, and delete them. We should not see any
  // cross-thread posts at this point. We should also see destruction in reverse order.
  TypedSlotPtr<> slot3 = TypedSlot<>::makeUnique(tls_);
  TestThreadLocalObject& object_ref3 = setObject(*slot3);
  TypedSlotPtr<> slot4 = TypedSlot<>::makeUnique(tls_);
  TestThreadLocalObject& object_ref4 = setObject(*slot4);

  tls_.shutdownGlobalThreading();
  slot3.reset();
  slot4.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 0);

  EXPECT_CALL(object_ref4, onDestroy());
  EXPECT_CALL(object_ref3, onDestroy());
  tls_.shutdownThread();
}

struct ThreadStatus {
  uint64_t thread_local_calls_{0};
  bool all_threads_complete_ = false;
};

// Test helper class for running two similar tests, covering 4 variants of
// runOnAllThreads: with/without completion callback, and with/without the slot
// data as an argument.
class CallbackNotInvokedAfterDeletionTest : public ThreadLocalInstanceImplTest {
protected:
  CallbackNotInvokedAfterDeletionTest() : slot_(TypedSlot<>::makeUnique(tls_)) {
    EXPECT_CALL(thread_dispatcher_, post(_)).Times(4).WillRepeatedly(Invoke([&](Event::PostCb cb) {
      // Holds the posted callback.
      holder_.push_back(cb);
    }));

    slot_->set([this](Event::Dispatcher&) {
      // Callbacks happen on the main thread but not the workers, so track the total.
      total_callbacks_++;
      return std::make_shared<ThreadLocalObject>();
    });
  }

  ~CallbackNotInvokedAfterDeletionTest() override {
    EXPECT_FALSE(thread_status_.all_threads_complete_);
    EXPECT_EQ(2, total_callbacks_);
    slot_.reset();
    EXPECT_EQ(freeSlotIndexesListSize(), 1);

    EXPECT_CALL(main_dispatcher_, post(_));
    while (!holder_.empty()) {
      holder_.front()();
      holder_.pop_front();
    }
    EXPECT_EQ(2, total_callbacks_);
    EXPECT_TRUE(thread_status_.all_threads_complete_);

    tls_.shutdownGlobalThreading();
  }

  // Allocate a slot and invoke all callback variants. Hold all callbacks and destroy the slot.
  // Make sure that recycling happens appropriately.
  TypedSlotPtr<> slot_;
  std::list<Event::PostCb> holder_;
  uint32_t total_callbacks_{0};
  ThreadStatus thread_status_;
};

TEST_F(CallbackNotInvokedAfterDeletionTest, WithData) {
  InSequence s;
  slot_->runOnAllThreads([this](OptRef<ThreadLocalObject> obj) {
    EXPECT_TRUE(obj.has_value());
    // Callbacks happen on the main thread but not the workers, so track the total.
    total_callbacks_++;
  });
  slot_->runOnAllThreads(
      [this](OptRef<ThreadLocalObject> obj) {
        EXPECT_TRUE(obj.has_value());
        ++thread_status_.thread_local_calls_;
      },
      [this]() {
        // Callbacks happen on the main thread but not the workers.
        EXPECT_EQ(thread_status_.thread_local_calls_, 1);
        thread_status_.all_threads_complete_ = true;
      });
}

// Test that the update callback is called as expected, for the worker and main threads.
TEST_F(ThreadLocalInstanceImplTest, UpdateCallback) {
  InSequence s;

  TypedSlot<> slot(tls_);

  uint32_t update_called = 0;

  TestThreadLocalObject& object_ref = setObject(slot);
  auto update_cb = [&update_called](OptRef<ThreadLocalObject>) { ++update_called; };
  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(object_ref, onDestroy());
  slot.runOnAllThreads(update_cb);

  EXPECT_EQ(2, update_called); // 1 worker, 1 main thread.

  tls_.shutdownGlobalThreading();
  tls_.shutdownThread();
}

struct StringSlotObject : public ThreadLocalObject {
  std::string str_;
};

TEST_F(ThreadLocalInstanceImplTest, TypedUpdateCallback) {
  InSequence s;
  TypedSlot<StringSlotObject> slot(tls_);

  uint32_t update_called = 0;
  EXPECT_CALL(thread_dispatcher_, post(_));
  slot.set([](Event::Dispatcher&) -> std::shared_ptr<StringSlotObject> {
    auto s = std::make_shared<StringSlotObject>();
    s->str_ = "hello";
    return s;
  });
  EXPECT_EQ("hello", slot.get()->str_);

  auto update_cb = [&update_called](OptRef<StringSlotObject> s) {
    ++update_called;
    EXPECT_TRUE(s.has_value());
    s->str_ = "goodbye";
  };
  EXPECT_CALL(thread_dispatcher_, post(_));
  slot.runOnAllThreads(update_cb);

  // Tests a few different ways of getting at the slot data.
  EXPECT_EQ("goodbye", slot.get()->str_);
  EXPECT_EQ("goodbye", slot->str_);
  EXPECT_EQ("goodbye", (*slot).str_);
  EXPECT_EQ(2, update_called); // 1 worker, 1 main thread.

  tls_.shutdownGlobalThreading();
  tls_.shutdownThread();
}

TEST_F(ThreadLocalInstanceImplTest, NoDataCallback) {
  InSequence s;
  TypedSlot<StringSlotObject> slot(tls_);

  uint32_t update_called = 0;
  EXPECT_CALL(thread_dispatcher_, post(_));
  slot.set([](Event::Dispatcher&) -> std::shared_ptr<StringSlotObject> { return nullptr; });
  EXPECT_FALSE(slot.get().has_value());

  auto update_cb = [&update_called](OptRef<StringSlotObject> s) {
    ++update_called;
    EXPECT_FALSE(s.has_value());
  };
  EXPECT_CALL(thread_dispatcher_, post(_));
  slot.runOnAllThreads(update_cb);

  EXPECT_FALSE(slot.get().has_value());
  EXPECT_EQ(2, update_called); // 1 worker, 1 main thread.

  tls_.shutdownGlobalThreading();
  tls_.shutdownThread();
}

// TODO(ramaraochavali): Run this test with real threads. The current issue in the unit
// testing environment is, the post to main_dispatcher is not working as expected.

// Validate ThreadLocal::runOnAllThreads behavior with all_thread_complete call back.
TEST_F(ThreadLocalInstanceImplTest, RunOnAllThreads) {
  TypedSlotPtr<> tlsptr = TypedSlot<>::makeUnique(tls_);
  TestThreadLocalObject& object_ref = setObject(*tlsptr);

  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(main_dispatcher_, post(_));

  // Ensure that the thread local call back and all_thread_complete call back are called.
  ThreadStatus thread_status;
  tlsptr->runOnAllThreads(
      [&thread_status](OptRef<ThreadLocalObject>) { ++thread_status.thread_local_calls_; },
      [&thread_status]() {
        EXPECT_EQ(thread_status.thread_local_calls_, 2);
        thread_status.all_threads_complete_ = true;
      });
  EXPECT_TRUE(thread_status.all_threads_complete_);

  tls_.shutdownGlobalThreading();
  tlsptr.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 0);
  EXPECT_CALL(object_ref, onDestroy());
  tls_.shutdownThread();
}

// Validate ThreadLocal::InstanceImpl's dispatcher() behavior.
TEST(ThreadLocalInstanceImplDispatcherTest, Dispatcher) {
  InstanceImpl tls;

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr main_dispatcher(api->allocateDispatcher("test_main_thread"));
  Event::DispatcherPtr thread_dispatcher(api->allocateDispatcher("test_worker_thread"));

  tls.registerThread(*main_dispatcher, true);
  tls.registerThread(*thread_dispatcher, false);

  // Ensure that the dispatcher update in tls posted during the above registerThread happens.
  main_dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  // Verify we have the expected dispatcher for the main thread.
  EXPECT_EQ(main_dispatcher.get(), &tls.dispatcher());

  Thread::ThreadPtr thread =
      Thread::threadFactoryForTest().createThread([&thread_dispatcher, &tls]() {
        // Ensure that the dispatcher update in tls posted during the above registerThread happens.
        thread_dispatcher->run(Event::Dispatcher::RunType::NonBlock);
        // Verify we have the expected dispatcher for the new thread thread.
        EXPECT_EQ(thread_dispatcher.get(), &tls.dispatcher());
        // Verify that it is inside the worker thread.
        EXPECT_FALSE(Thread::MainThread::isMainThread());
      });
  thread->join();

  // Verify we still have the expected dispatcher for the main thread.
  EXPECT_EQ(main_dispatcher.get(), &tls.dispatcher());

  tls.shutdownGlobalThreading();
  tls.shutdownThread();
}

} // namespace ThreadLocal
} // namespace Envoy
