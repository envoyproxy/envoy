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
  MOCK_METHOD(bool, tryAllocateResource, (OverloadProactiveResourceName, int64_t));
  MOCK_METHOD(bool, tryDeallocateResource, (OverloadProactiveResourceName, int64_t));
  MOCK_METHOD(bool, isResourceMonitorEnabled, (OverloadProactiveResourceName));
  MOCK_METHOD(ProactiveResourceMonitorOptRef, getProactiveResourceMonitorForTest,
              (OverloadProactiveResourceName));

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
  MOCK_METHOD(Event::ScaledRangeTimerManagerFactory, scaledTimerFactory, (), (override));
  MOCK_METHOD(ThreadLocalOverloadState&, getThreadLocalOverloadState, ());
  MOCK_METHOD(LoadShedPoint*, getLoadShedPoint, (absl::string_view));
  MOCK_METHOD(void, stop, ());

  testing::NiceMock<MockThreadLocalOverloadState> overload_state_;
};

class MockLoadShedPoint : public LoadShedPoint {
public:
  MockLoadShedPoint() = default;

  // LoadShedPoint
  MOCK_METHOD(bool, shouldShedLoad, ());
};

} // namespace Server
} // namespace Envoy
