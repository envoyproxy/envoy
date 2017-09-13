#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Event {

MockDispatcher::MockDispatcher() {
  ON_CALL(*this, clearDeferredDeleteList()).WillByDefault(Invoke([this]() -> void {
    to_delete_.clear();
  }));
  ON_CALL(*this, createTimer_(_)).WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
  ON_CALL(*this, post(_)).WillByDefault(Invoke([](PostCb cb) -> void { cb(); }));
}

MockDispatcher::~MockDispatcher() {}

MockTimer::MockTimer() {}

// Ownership of each MockTimer instance is transferred to the (caller of) dispatcher's
// createTimer_(), so to avoid destructing it twice, the MockTimer must have been dynamically
// allocated and must not be deleted by it's creator.
MockTimer::MockTimer(MockDispatcher* dispatcher) {
  EXPECT_CALL(*dispatcher, createTimer_(_))
      .WillOnce(DoAll(SaveArg<0>(&callback_), Return(this)))
      .RetiresOnSaturation();
}

MockTimer::~MockTimer() {}

MockSignalEvent::MockSignalEvent(MockDispatcher* dispatcher) {
  EXPECT_CALL(*dispatcher, listenForSignal_(_, _))
      .WillOnce(DoAll(SaveArg<1>(&callback_), Return(this)))
      .RetiresOnSaturation();
}

MockSignalEvent::~MockSignalEvent() {}

MockFileEvent::MockFileEvent() {}
MockFileEvent::~MockFileEvent() {}

} // namespace Event
} // namespace Envoy
