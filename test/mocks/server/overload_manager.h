#pragma once

#include <string>

#include "envoy/server/overload/overload_manager.h"
#include "envoy/server/overload/thread_local_overload_state.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class MockThreadLocalOverloadState : public ThreadLocalOverloadState {
public:
  MockThreadLocalOverloadState();
  MOCK_METHOD(const OverloadActionState&, getState, (const std::string&), (override));
  Event::TimerPtr createScaledTimer(Event::ScaledTimerMinimum minimum,
                                    Event::TimerCb callback) override;
  MOCK_METHOD(Event::Timer*, createScaledMinimumTimer_,
              (Event::ScaledTimerMinimum, Event::TimerCb));

private:
  const OverloadActionState disabled_state_;
};

class MockOverloadManager : public OverloadManager {
public:
  MockOverloadManager();
  ~MockOverloadManager() override;

  // OverloadManager
  MOCK_METHOD(void, start, ());
  MOCK_METHOD(bool, registerForAction,
              (const std::string& action, Event::Dispatcher& dispatcher,
               OverloadActionCb callback));
  MOCK_METHOD(ThreadLocalOverloadState&, getThreadLocalOverloadState, ());
  MOCK_METHOD(Event::ScaledTimerMinimum, getConfiguredTimerMinimum, (OverloadTimerType timer_type),
              (const));

  testing::NiceMock<MockThreadLocalOverloadState> overload_state_;
};

} // namespace Server
} // namespace Envoy
