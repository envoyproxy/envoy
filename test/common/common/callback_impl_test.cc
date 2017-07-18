#include "common/common/callback_impl.h"

#include "gmock/gmock.h"

using testing::InSequence;

namespace Envoy {
namespace Common {

class CallbackManagerTest : public testing::Test {
public:
  MOCK_METHOD1(called, void(int arg));
};

TEST_F(CallbackManagerTest, All) {
  InSequence s;

  CallbackManager<int> manager;
  CallbackHandle* handle1 = manager.add([this](int arg) -> void { called(arg); });
  manager.add([this](int arg) -> void { called(arg * 2); });

  EXPECT_CALL(*this, called(5));
  EXPECT_CALL(*this, called(10));
  manager.runCallbacks(5);

  handle1->remove();
  EXPECT_CALL(*this, called(10));
  manager.runCallbacks(5);

  EXPECT_CALL(*this, called(10));
  EXPECT_CALL(*this, called(20));
  CallbackHandle* handle3 = manager.add([this, &handle3](int arg) -> void {
    called(arg * 4);
    handle3->remove();
  });
  manager.runCallbacks(5);

  EXPECT_CALL(*this, called(10));
  manager.runCallbacks(5);
}

} // namespace Common
} // namespace Envoy
