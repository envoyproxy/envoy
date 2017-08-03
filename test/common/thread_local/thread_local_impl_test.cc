#include "common/thread_local/thread_local_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"

using testing::InSequence;
using testing::Ref;
using testing::ReturnPointee;
using testing::StrictMock;
using testing::_;

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
  StrictMock<Event::MockDispatcher> main_dispatcher_;
  StrictMock<Event::MockDispatcher> thread_dispatcher_;
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

} // namespace ThreadLocal
} // namespace Envoy
