#include "common/callback/callback.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Common {
namespace Callback {
namespace {

using R = Receiver<uint32_t>;
using C = Caller<uint32_t>;

struct MockClient {
  MOCK_CONST_METHOD1(callback, void(uint32_t));
  R receiver_{[this](uint32_t data) { callback(data); }};
};

TEST(CallbackTest, CallAvailableReceiver) {
  MockClient client;
  EXPECT_TRUE(client.receiver_);

  C caller(client.receiver_.caller());
  EXPECT_TRUE(caller);

  EXPECT_CALL(client, callback(123));
  caller(123);
}

TEST(CallbackTest, CallDestroyedReceiver) {
  C caller;
  {
    MockClient client;
    caller = client.receiver_.caller();
    EXPECT_CALL(client, callback(_)).Times(0);
  }
  EXPECT_FALSE(caller);
  caller(123);
}

TEST(CallbackTest, CallResetReceiver) {
  MockClient client;
  C caller(client.receiver_.caller());
  client.receiver_.reset();

  EXPECT_FALSE(client.receiver_);
  EXPECT_FALSE(caller);
  EXPECT_CALL(client, callback(_)).Times(0);
  caller(123);
}

TEST(CallbackTest, DefaultInitializedCaller) {
  C caller;
  EXPECT_FALSE(caller);
}

TEST(CallbackTest, ResetCaller) {
  R receiver{[](uint32_t) {}};
  C caller(receiver.caller());
  caller.reset();
  EXPECT_FALSE(caller);
}

} // namespace
} // namespace Callback
} // namespace Common
} // namespace Envoy
