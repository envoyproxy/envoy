#include "test/mocks/server/overload_manager.h"

#include <string>

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::NiceMock;
using ::testing::ReturnNew;
using ::testing::ReturnRef;

MockThreadLocalOverloadState::MockThreadLocalOverloadState()
    : disabled_state_(OverloadActionState::inactive()) {
  ON_CALL(*this, getState).WillByDefault(ReturnRef(disabled_state_));
  ON_CALL(*this, createScaledTypedTimer_).WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
  ON_CALL(*this, createScaledMinimumTimer_).WillByDefault(ReturnNew<NiceMock<Event::MockTimer>>());
}

Event::TimerPtr MockThreadLocalOverloadState::createScaledTimer(OverloadTimerType timer_type,
                                                                Event::TimerCb callback) {
  return Event::TimerPtr{createScaledTypedTimer_(timer_type, std::move(callback))};
}

Event::TimerPtr MockThreadLocalOverloadState::createScaledTimer(Event::ScaledTimerMinimum minimum,
                                                                Event::TimerCb callback) {
  return Event::TimerPtr{createScaledMinimumTimer_(minimum, std::move(callback))};
}

MockOverloadManager::MockOverloadManager() {
  ON_CALL(*this, getThreadLocalOverloadState()).WillByDefault(ReturnRef(overload_state_));
}

MockOverloadManager::~MockOverloadManager() = default;

} // namespace Server
} // namespace Envoy
