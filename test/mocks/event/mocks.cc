#include "mocks.h"

#include "common/network/listen_socket_impl.h"
#include "common/stats/stats_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Lyft {
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::SaveArg;

namespace Event {

MockDispatcher::MockDispatcher() {
  ON_CALL(*this, clearDeferredDeleteList())
      .WillByDefault(Invoke([this]() -> void { to_delete_.clear(); }));
  ON_CALL(*this, createTimer_(_)).WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
  ON_CALL(*this, post(_)).WillByDefault(Invoke([](PostCb cb) -> void { cb(); }));
}

MockDispatcher::~MockDispatcher() {}

MockTimer::MockTimer() {}

MockTimer::MockTimer(MockDispatcher* dispatcher) {
  EXPECT_CALL(*dispatcher, createTimer_(_))
      .WillOnce(DoAll(SaveArg<0>(&callback_), Return(this)))
      .RetiresOnSaturation();
}

MockTimer::~MockTimer() {}

} // Event
} // Lyft