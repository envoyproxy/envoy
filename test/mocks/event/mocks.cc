#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnPointee;
using testing::SaveArg;

namespace Envoy {
namespace Event {

MockDispatcher::MockDispatcher() : MockDispatcher("test_thread") {}

MockDispatcher::MockDispatcher(const std::string& name) : name_(name) {
  ON_CALL(*this, initializeStats(_, _)).WillByDefault(Return());
  ON_CALL(*this, clearDeferredDeleteList()).WillByDefault(Invoke([this]() -> void {
    to_delete_.clear();
  }));
  ON_CALL(*this, createTimer_(_)).WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
  ON_CALL(*this, createScaledTimer_(_, _)).WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
  ON_CALL(*this, createScaledTypedTimer_(_, _))
      .WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
  ON_CALL(*this, post(_)).WillByDefault(Invoke([](PostCb cb) -> void { cb(); }));

  ON_CALL(buffer_factory_, createBuffer_(_, _, _))
      .WillByDefault(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                               std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));
  ON_CALL(*this, isThreadSafe()).WillByDefault(Return(true));
}

MockDispatcher::~MockDispatcher() = default;

MockTimer::MockTimer() {
  ON_CALL(*this, enableTimer(_, _))
      .WillByDefault(Invoke([&](const std::chrono::milliseconds&, const ScopeTrackedObject* scope) {
        enabled_ = true;
        scope_ = scope;
      }));
  ON_CALL(*this, disableTimer()).WillByDefault(Assign(&enabled_, false));
  ON_CALL(*this, enabled()).WillByDefault(ReturnPointee(&enabled_));
}

MockTimer::MockTimer(MockDispatcher* dispatcher) : MockTimer() {
  dispatcher_ = dispatcher;
  EXPECT_CALL(*dispatcher, createTimer_(_))
      .WillOnce(DoAll(SaveArg<0>(&callback_), Return(this)))
      .RetiresOnSaturation();
  ON_CALL(*this, enableTimer(_, _))
      .WillByDefault(Invoke([&](const std::chrono::milliseconds&, const ScopeTrackedObject* scope) {
        enabled_ = true;
        scope_ = scope;
      }));
  ON_CALL(*this, disableTimer()).WillByDefault(Assign(&enabled_, false));
  ON_CALL(*this, enabled()).WillByDefault(ReturnPointee(&enabled_));
}

MockTimer::~MockTimer() {
  if (timer_destroyed_) {
    *timer_destroyed_ = true;
  }
}

MockSchedulableCallback::~MockSchedulableCallback() = default;

MockSchedulableCallback::MockSchedulableCallback(MockDispatcher* dispatcher)
    : dispatcher_(dispatcher) {
  EXPECT_CALL(*dispatcher, createSchedulableCallback_(_))
      .WillOnce(DoAll(SaveArg<0>(&callback_), Return(this)))
      .RetiresOnSaturation();
  ON_CALL(*this, scheduleCallbackCurrentIteration()).WillByDefault(Assign(&enabled_, true));
  ON_CALL(*this, scheduleCallbackNextIteration()).WillByDefault(Assign(&enabled_, true));
  ON_CALL(*this, cancel()).WillByDefault(Assign(&enabled_, false));
  ON_CALL(*this, enabled()).WillByDefault(ReturnPointee(&enabled_));
}

MockSignalEvent::MockSignalEvent(MockDispatcher* dispatcher) {
  EXPECT_CALL(*dispatcher, listenForSignal_(_, _))
      .WillOnce(DoAll(SaveArg<1>(&callback_), Return(this)))
      .RetiresOnSaturation();
}

MockSignalEvent::~MockSignalEvent() = default;

MockFileEvent::MockFileEvent() = default;
MockFileEvent::~MockFileEvent() = default;

} // namespace Event
} // namespace Envoy
