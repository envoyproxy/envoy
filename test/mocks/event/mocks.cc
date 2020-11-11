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
  ON_CALL(*this, post(_)).WillByDefault(Invoke([](PostCb cb) -> void { cb(); }));
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

// Ownership of each MockTimer instance is transferred to the (caller of) dispatcher's
// createTimer_(), so to avoid destructing it twice, the MockTimer must have been dynamically
// allocated and must not be deleted by it's creator.
MockTimer::MockTimer(MockDispatcher* dispatcher) : MockTimer() {
  dispatcher_ = dispatcher;
  EXPECT_CALL(*dispatcher, createTimer_(_))
      .WillOnce(DoAll(SaveArg<0>(&callback_), Return(this)))
      .RetiresOnSaturation();
}

MockTimer::~MockTimer() = default;

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
