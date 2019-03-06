#include <cstdint>
#include <memory>

#include "common/callback/manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Common {
namespace Callback {
namespace {

struct MockReceiver {
  MOCK_CONST_METHOD0(boolConversionOperator, bool());
  MOCK_CONST_METHOD1(functionCallOperator, void(uint32_t));
};

struct MockCaller {
  MockCaller(const MockReceiver& receiver) : receiver_(receiver) {}
  operator bool() const { return receiver_.boolConversionOperator(); }
  void operator()(uint32_t data) const { receiver_.functionCallOperator(data); }
  const MockReceiver& receiver_;
};

using M = ManagerT<MockCaller, uint32_t>;

TEST(ManagerTest, RemoveUnavailable) {
  InSequence s;
  M mgr;

  MockReceiver good_receivers[5] = {}, bad_receivers[5] = {};
  for (const auto& receiver : good_receivers) {
    // "good" receivers are invoked, and remain available after invocation
    EXPECT_CALL(receiver, functionCallOperator(123));
    EXPECT_CALL(receiver, boolConversionOperator()).WillOnce(Return(true));
    mgr.add(MockCaller(receiver));
  }
  for (const auto& receiver : bad_receivers) {
    // "bad" receivers are invoked, and become unavailable after invocation
    EXPECT_CALL(receiver, functionCallOperator(123));
    EXPECT_CALL(receiver, boolConversionOperator()).WillOnce(Return(false));
    mgr.add(MockCaller(receiver));
  }
  mgr(123);

  for (const auto& receiver : good_receivers) {
    // "good" receivers are invoked a second time
    EXPECT_CALL(receiver, functionCallOperator(456));
    EXPECT_CALL(receiver, boolConversionOperator());
  }
  for (const auto& receiver : bad_receivers) {
    // "bad" receivers have had their callers removed from the manager
    EXPECT_CALL(receiver, functionCallOperator(_)).Times(0);
    EXPECT_CALL(receiver, boolConversionOperator()).Times(0);
  }
  mgr(456);
}

} // namespace
} // namespace Callback
} // namespace Common
} // namespace Envoy
