#include "common/common/callback_impl.h"

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

} // namespace Common
} // namespace Envoy
