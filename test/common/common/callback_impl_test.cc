#include <thread>
#include <vector>

#include "source/common/common/callback_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;

namespace Envoy {
namespace Common {

class CallbackManagerTest : public testing::Test {
public:
  MOCK_METHOD(void, called, (int arg));
};

TEST_F(CallbackManagerTest, All) {
  InSequence s;

  CallbackManager<int> manager;
  auto handle1 = manager.add([this](int arg) -> void { called(arg); });
  auto handle2 = manager.add([this](int arg) -> void { called(arg * 2); });

  EXPECT_CALL(*this, called(5));
  EXPECT_CALL(*this, called(10));
  manager.runCallbacks(5);

  handle1.reset();
  EXPECT_CALL(*this, called(10));
  manager.runCallbacks(5);

  EXPECT_CALL(*this, called(10));
  EXPECT_CALL(*this, called(20));
  CallbackHandlePtr handle3 = manager.add([this, &handle3](int arg) -> void {
    called(arg * 4);
    handle3.reset();
  });
  manager.runCallbacks(5);

  EXPECT_CALL(*this, called(10));
  manager.runCallbacks(5);
}

TEST_F(CallbackManagerTest, DestroyManagerBeforeHandle) {
  CallbackHandlePtr handle;
  {
    CallbackManager<int> manager;
    handle = manager.add([this](int arg) -> void { called(arg); });
    EXPECT_CALL(*this, called(5));
    manager.runCallbacks(5);
  }
  EXPECT_NE(nullptr, handle);
  // It should be safe to destruct the handle after the manager.
  handle.reset();
}

class ThreadSafeCallbackManagerTest : public testing::Test {
public:
  MOCK_METHOD(void, called, (int arg));
};

// Test basic behaviors of the thread-safe callback-manager with respect to callback registration,
// de-registration, and execution.
TEST_F(ThreadSafeCallbackManagerTest, All) {
  InSequence s;

  testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
  ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

  auto manager = ThreadSafeCallbackManager::create();

  auto handle1 = manager->add(cb_dispatcher, [this]() -> void { called(5); });
  auto handle2 = manager->add(cb_dispatcher, [this]() -> void { called(10); });

  EXPECT_CALL(*this, called(5));
  EXPECT_CALL(*this, called(10));
  manager->runCallbacks();

  handle1.reset();
  EXPECT_CALL(*this, called(10));
  manager->runCallbacks();

  EXPECT_CALL(*this, called(10));
  EXPECT_CALL(*this, called(20));
  auto handle3 = manager->add(cb_dispatcher, [this]() -> void { called(20); });
  manager->runCallbacks();
  handle3.reset();

  EXPECT_CALL(*this, called(10));
  manager->runCallbacks();
}

// Validate that the handles returned from callback-registration can outlive the manager
// and can be destructed without error.
TEST_F(ThreadSafeCallbackManagerTest, DestroyManagerBeforeHandle) {
  testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
  ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

  CallbackHandlePtr handle;
  {
    auto manager = ThreadSafeCallbackManager::create();
    handle = manager->add(cb_dispatcher, [this]() -> void { called(5); });
    EXPECT_CALL(*this, called(5));
    manager->runCallbacks();
  }
  EXPECT_NE(nullptr, handle);
  // It should be safe to destruct the handle after the manager.
  handle.reset();
}

// Validate that a callback added and removed from a thread (and thus dispatcher) that
// no longer exist is a safe operation.
TEST_F(ThreadSafeCallbackManagerTest, RegisterAndRemoveOnExpiredThread) {
  auto manager = ThreadSafeCallbackManager::create();

  testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
  ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

  // Register a callback in a new thread and then remove it
  auto t = std::thread([this, manager = manager.get()] {
    testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
    ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

    auto handle = manager->add(cb_dispatcher, [this]() { called(20); });
    handle.reset();
  });

  // Add another callback on the main thread
  auto handle = manager->add(cb_dispatcher, [this]() { called(10); });

  // Validate that we can wait for the above thread to terminate (and de-register the
  // callback), then run the remaining callbacks.
  t.join();
  EXPECT_CALL(*this, called(10));
  manager->runCallbacks();
}

} // namespace Common
} // namespace Envoy
