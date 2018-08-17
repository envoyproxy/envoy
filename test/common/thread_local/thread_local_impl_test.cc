#include "common/event/dispatcher_impl.h"
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
  ~TestThreadLocalObject() { onDestroy(); }

  MOCK_METHOD0(onDestroy, void());
};

class ThreadLocalInstanceImplTest : public testing::Test {
public:
  ThreadLocalInstanceImplTest() {
    tls_.registerThread(main_dispatcher_, true);
    EXPECT_EQ(&main_dispatcher_, &tls_.dispatcher());
    EXPECT_CALL(thread_dispatcher_, post(_));
    tls_.registerThread(thread_dispatcher_, false);
  }

  MOCK_METHOD1(createThreadLocal, ThreadLocalObjectSharedPtr(Event::Dispatcher& dispatcher));

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

  InstanceImpl tls_;
  Event::MockDispatcher main_dispatcher_;
  Event::MockDispatcher thread_dispatcher_;
};

TEST_F(ThreadLocalInstanceImplTest, All) {
  InSequence s;

  // Free a slot without ever calling set.
  EXPECT_CALL(thread_dispatcher_, post(_));
  SlotPtr slot1 = tls_.allocateSlot();
  slot1.reset();

  // Create a new slot which should take the place of the old slot. ReturnPointee() is used to
  // avoid "leaks" when using InSequence and shared_ptr.
  SlotPtr slot2 = tls_.allocateSlot();
  TestThreadLocalObject& object_ref2 = setObject(*slot2);

  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(object_ref2, onDestroy());
  slot2.reset();

  // Make two new slots, shutdown global threading, and delete them. We should not see any
  // cross-thread posts at this point. We should also see destruction in reverse order.
  SlotPtr slot3 = tls_.allocateSlot();
  TestThreadLocalObject& object_ref3 = setObject(*slot3);
  SlotPtr slot4 = tls_.allocateSlot();
  TestThreadLocalObject& object_ref4 = setObject(*slot4);

  tls_.shutdownGlobalThreading();
  slot3.reset();
  slot4.reset();

  EXPECT_CALL(object_ref4, onDestroy());
  EXPECT_CALL(object_ref3, onDestroy());
  tls_.shutdownThread();
}

// TODO(ramaraochavali): Run this test with real threads. The current issue in the unit
// testing environment is, the post to main_dispatcher is not working as expected.

// Validate ThreadLocal::runOnAllThreads behavior with all_thread_complete call back.
TEST_F(ThreadLocalInstanceImplTest, RunOnAllThreads) {
  SlotPtr tlsptr = tls_.allocateSlot();

  EXPECT_CALL(thread_dispatcher_, post(_));
  EXPECT_CALL(main_dispatcher_, post(_));

  // Ensure that the thread local call back and all_thread_complete call back are called.
  struct {
    uint64_t thread_local_calls_{0};
    bool all_threads_complete_ = false;
  } thread_status;

  tlsptr->runOnAllThreads([&thread_status]() -> void { ++thread_status.thread_local_calls_; },
                          [&thread_status]() -> void {
                            EXPECT_EQ(thread_status.thread_local_calls_, 2);
                            thread_status.all_threads_complete_ = true;
                          });

  EXPECT_TRUE(thread_status.all_threads_complete_);

  tls_.shutdownGlobalThreading();
  tls_.shutdownThread();
}

// Validate ThreadLocal::InstanceImpl's dispatcher() behavior.
TEST(ThreadLocalInstanceImplDispatcherTest, Dispatcher) {
  InstanceImpl tls;
  Event::DispatcherImpl main_dispatcher;
  Event::DispatcherImpl thread_dispatcher;

  tls.registerThread(main_dispatcher, true);
  tls.registerThread(thread_dispatcher, false);

  // Ensure that the dispatcher update in tls posted during the above registerThread happens.
  main_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
  // Verify we have the expected dispatcher for the main thread.
  EXPECT_EQ(&main_dispatcher, &tls.dispatcher());

  Thread::Thread([&thread_dispatcher, &tls]() {
    // Ensure that the dispatcher update in tls posted during the above registerThread happens.
    thread_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    // Verify we have the expected dispatcher for the new thread thread.
    EXPECT_EQ(&thread_dispatcher, &tls.dispatcher());
  })
      .join();

  // Verify we still have the expected dispatcher for the main thread.
  EXPECT_EQ(&main_dispatcher, &tls.dispatcher());

  tls.shutdownGlobalThreading();
  tls.shutdownThread();
}

} // namespace ThreadLocal
} // namespace Envoy
