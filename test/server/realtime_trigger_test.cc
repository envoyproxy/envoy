#include "source/server/realtime_trigger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

using ::testing::Return;
using ::testing::StrictMock;

class MockTrigger : public Trigger {
public:
  MOCK_METHOD(bool, updateValue, (double), (override));
  MOCK_METHOD(OverloadActionState, evaluate, (double), (const, override));
  MOCK_METHOD(OverloadActionState, actionState, (), (const, override));
};

class MockRealtimeResourceMonitor : public RealtimeResourceMonitor {
public:
  MOCK_METHOD(ResourceUsage, getResourceUsage, (), (override));
  MOCK_METHOD(void, onLoadAccepted, (absl::string_view), (override));
};

TEST(RealtimeTriggerTest, EvaluatesTriggerAndNotifiesOnLoadAccepted) {
  auto trigger = std::make_unique<StrictMock<MockTrigger>>();
  auto* trigger_ptr = trigger.get();
  auto monitor = std::make_shared<StrictMock<MockRealtimeResourceMonitor>>();
  RealtimeTrigger realtime_trigger(std::move(trigger), monitor);

  EXPECT_CALL(*monitor, getResourceUsage()).WillOnce(Return(ResourceUsage{0.5}));
  EXPECT_CALL(*trigger_ptr, evaluate(0.5)).WillOnce(Return(OverloadActionState::inactive()));
  EXPECT_FLOAT_EQ(0.0f, realtime_trigger.shedProbability());

  EXPECT_CALL(*monitor, getResourceUsage()).WillOnce(Return(ResourceUsage{0.7}));
  EXPECT_CALL(*trigger_ptr, evaluate(0.7)).WillOnce(Return(OverloadActionState(UnitFloat(0.5))));
  EXPECT_FLOAT_EQ(0.5f, realtime_trigger.shedProbability());

  EXPECT_CALL(*monitor, getResourceUsage()).WillOnce(Return(ResourceUsage{0.9}));
  EXPECT_CALL(*trigger_ptr, evaluate(0.9)).WillOnce(Return(OverloadActionState::saturated()));
  EXPECT_FLOAT_EQ(1.0f, realtime_trigger.shedProbability());

  EXPECT_CALL(*monitor, onLoadAccepted(absl::string_view("test_point")));
  realtime_trigger.onLoadAccepted("test_point");
}

} // namespace
} // namespace Server
} // namespace Envoy
