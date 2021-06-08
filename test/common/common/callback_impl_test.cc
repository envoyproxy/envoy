#include <vector>

#include "source/common/common/callback_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;

namespace Envoy {
namespace Common {

class CallbackManagerTest : public testing::TestWithParam<bool> {
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

TEST_F(CallbackManagerTest, ThreadSafe__All) {
  InSequence s;

  testing::NiceMock<Event::MockDispatcher> dispatcher;
  testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
  ON_CALL(dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));
  ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

  ThreadSafeCallbackManager<int> manager{dispatcher};

  auto handle1 = manager.add(cb_dispatcher, [this](int arg) -> void { called(arg); });
  auto handle2 = manager.add(cb_dispatcher, [this](int arg) -> void { called(arg * 2); });

  EXPECT_CALL(*this, called(5));
  EXPECT_CALL(*this, called(10));
  manager.runCallbacksWith([] { return 5; });

  handle1.reset();
  EXPECT_CALL(*this, called(10));
  manager.runCallbacksWith([] { return 5; });

  EXPECT_CALL(*this, called(10));
  EXPECT_CALL(*this, called(20));
  auto handle3 = manager.add(cb_dispatcher, [this](int arg) -> void { called(arg * 4); });
  manager.runCallbacksWith([] { return 5; });
  handle3.reset();

  EXPECT_CALL(*this, called(10));
  manager.runCallbacksWith([] { return 5; });
}

TEST_F(CallbackManagerTest, ThreadSafe__DestroyManagerBeforeHandle) {
  testing::NiceMock<Event::MockDispatcher> dispatcher;
  testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
  ON_CALL(dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));
  ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

  ThreadSafeCallbackHandlePtr handle;
  {
    ThreadSafeCallbackManager<int> manager{dispatcher};
    handle = manager.add(cb_dispatcher, [this](int arg) -> void { called(arg); });
    EXPECT_CALL(*this, called(5));
    manager.runCallbacksWith([] { return 5; });
  }
  EXPECT_NE(nullptr, handle);
  // It should be safe to destruct the handle after the manager.
  handle.reset();
}

TEST_F(CallbackManagerTest, ThreadSafe__RemoveCallbackAsync) {
  InSequence s;

  std::vector<std::function<void()>> remove_cbs;
  testing::NiceMock<Event::MockDispatcher> dispatcher;
  testing::NiceMock<Event::MockDispatcher> cb_dispatcher;
  ON_CALL(dispatcher, post(_)).WillByDefault(Invoke([&remove_cbs](std::function<void()> cb) {
    remove_cbs.push_back(cb);
  }));
  ON_CALL(cb_dispatcher, post(_)).WillByDefault(Invoke([](std::function<void()> cb) { cb(); }));

  ThreadSafeCallbackManager<int> manager{dispatcher};

  auto handle1 = manager.add(cb_dispatcher, [this](int arg) -> void { called(arg); });
  auto handle2 = manager.add(cb_dispatcher, [this](int arg) -> void { called(arg * 2); });

  // delete handle1, but delay removal from callback manager
  handle1.reset();
  EXPECT_CALL(*this, called(10));
  manager.runCallbacksWith([] { return 5; });

  // remove all callbacks
  handle2.reset();
  for (auto& cb : remove_cbs) {
    cb();
  }
}

} // namespace Common
} // namespace Envoy
