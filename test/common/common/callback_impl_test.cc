#include "source/common/common/callback_impl.h"

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
  auto handle1 = manager.add([this](int arg) {
    called(arg);
    return absl::OkStatus();
  });
  auto handle2 = manager.add([this](int arg) {
    called(arg * 2);
    return absl::OkStatus();
  });

  EXPECT_CALL(*this, called(5));
  EXPECT_CALL(*this, called(10));
  ASSERT_TRUE(manager.runCallbacks(5).ok());

  handle1.reset();
  EXPECT_CALL(*this, called(10));
  ASSERT_TRUE(manager.runCallbacks(5).ok());

  EXPECT_CALL(*this, called(10));
  EXPECT_CALL(*this, called(20));
  CallbackHandlePtr handle3 = manager.add([this, &handle3](int arg) {
    called(arg * 4);
    handle3.reset();
    return absl::OkStatus();
  });
  ASSERT_TRUE(manager.runCallbacks(5).ok());

  EXPECT_CALL(*this, called(10));
  ASSERT_TRUE(manager.runCallbacks(5).ok());
}

TEST_F(CallbackManagerTest, DestroyManagerBeforeHandle) {
  CallbackHandlePtr handle;
  {
    CallbackManager<int> manager;
    handle = manager.add([this](int arg) {
      called(arg);
      return absl::OkStatus();
    });
    EXPECT_CALL(*this, called(5));
    ASSERT_TRUE(manager.runCallbacks(5).ok());
  }
  EXPECT_NE(nullptr, handle);
  // It should be safe to destruct the handle after the manager.
  handle.reset();
}

} // namespace Common
} // namespace Envoy
