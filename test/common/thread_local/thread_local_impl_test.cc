#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"

using testing::_;
using testing::InSequence;
using testing::Ref;
using testing::ReturnPointee;

namespace Envoy {
namespace ThreadLocal {

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

  TestThreadLocalObject& setObject(Slot& slot) {
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
  SlotPtr slot1 = tls_.allocateSlot();
  slot1.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 1);

  // Create a new slot which should take the place of the old slot. ReturnPointee() is used to
  // avoid "leaks" when using InSequence and shared_ptr.
  SlotPtr slot2 = tls_.allocateSlot();
  TestThreadLocalObject& object_ref2 = setObject(*slot2);
  EXPECT_EQ(freeSlotIndexesListSize(), 0);

  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(object_ref2, onDestroy());
  EXPECT_EQ(freeSlotIndexesListSize(), 0);
  slot2.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 1);

  // Make two new slots, shutdown global threading, and delete them. We should not see any
  // cross-thread posts at this point. We should also see destruction in reverse order.
  SlotPtr slot3 = tls_.allocateSlot();
  TestThreadLocalObject& object_ref3 = setObject(*slot3);
  SlotPtr slot4 = tls_.allocateSlot();
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

TEST_F(ThreadLocalInstanceImplTest, CallbackNotInvokedAfterDeletion) {
  InSequence s;

  // Allocate a slot and invoke all callback variants. Hold all callbacks and destroy the slot.
  // Make sure that recycling happens appropriately.
  SlotPtr slot = tls_.allocateSlot();

  std::list<Event::PostCb> holder;
  EXPECT_CALL(thread_dispatcher_, post(_)).Times(4).WillRepeatedly(Invoke([&](Event::PostCb cb) {
    // Holds the posted callback.
    holder.push_back(cb);
  }));

  uint32_t total_callbacks = 0;
  slot->set([&total_callbacks](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    // Callbacks happen on the main thread but not the workers, so track the total.
    total_callbacks++;
    return nullptr;
  });
  slot->runOnAllThreads([&total_callbacks](ThreadLocal::ThreadLocalObjectSharedPtr)
                            -> ThreadLocal::ThreadLocalObjectSharedPtr {
    // Callbacks happen on the main thread but not the workers, so track the total.
    total_callbacks++;
    return nullptr;
  });
  ThreadStatus thread_status;
  slot->runOnAllThreads(
      [&thread_status](
          ThreadLocal::ThreadLocalObjectSharedPtr) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        ++thread_status.thread_local_calls_;
        return nullptr;
      },
      [&thread_status]() -> void {
        // Callbacks happen on the main thread but not the workers.
        EXPECT_EQ(thread_status.thread_local_calls_, 1);
        thread_status.all_threads_complete_ = true;
      });
  EXPECT_FALSE(thread_status.all_threads_complete_);

  EXPECT_EQ(2, total_callbacks);
  slot.reset();
  EXPECT_EQ(freeSlotIndexesListSize(), 1);

  EXPECT_CALL(main_dispatcher_, post(_));
  while (!holder.empty()) {
    holder.front()();
    holder.pop_front();
  }
  EXPECT_EQ(2, total_callbacks);
  EXPECT_TRUE(thread_status.all_threads_complete_);

  tls_.shutdownGlobalThreading();
}

// Test that the config passed into the update callback is the previous version stored in the slot.
TEST_F(ThreadLocalInstanceImplTest, UpdateCallback) {
  InSequence s;

  SlotPtr slot = tls_.allocateSlot();

  auto newer_version = std::make_shared<TestThreadLocalObject>();
  bool update_called = false;

  TestThreadLocalObject& object_ref = setObject(*slot);
  auto update_cb = [&object_ref, &update_called,
                    newer_version](ThreadLocalObjectSharedPtr obj) -> ThreadLocalObjectSharedPtr {
    // The unit test setup have two dispatchers registered, but only one thread, this lambda will be
    // called twice in the same thread.
    if (!update_called) {
      EXPECT_EQ(obj.get(), &object_ref);
      update_called = true;
    } else {
      EXPECT_EQ(obj.get(), newer_version.get());
    }

    return newer_version;
  };
  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(object_ref, onDestroy());
  EXPECT_CALL(*newer_version, onDestroy());
  slot->runOnAllThreads(update_cb);

  EXPECT_EQ(newer_version.get(), &slot->getTyped<TestThreadLocalObject>());

  tls_.shutdownGlobalThreading();
  tls_.shutdownThread();
}

// TODO(ramaraochavali): Run this test with real threads. The current issue in the unit
// testing environment is, the post to main_dispatcher is not working as expected.

// Validate ThreadLocal::runOnAllThreads behavior with all_thread_complete call back.
TEST_F(ThreadLocalInstanceImplTest, RunOnAllThreads) {
  SlotPtr tlsptr = tls_.allocateSlot();
  TestThreadLocalObject& object_ref = setObject(*tlsptr);

  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(main_dispatcher_, post(_));

  // Ensure that the thread local call back and all_thread_complete call back are called.
  ThreadStatus thread_status;
  tlsptr->runOnAllThreads(
      [&thread_status](ThreadLocal::ThreadLocalObjectSharedPtr object)
          -> ThreadLocal::ThreadLocalObjectSharedPtr {
        ++thread_status.thread_local_calls_;
        return object;
      },
      [&thread_status]() -> void {
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
      });
  thread->join();

  // Verify we still have the expected dispatcher for the main thread.
  EXPECT_EQ(main_dispatcher.get(), &tls.dispatcher());

  tls.shutdownGlobalThreading();
  tls.shutdownThread();
}

} // namespace ThreadLocal
} // namespace Envoy
