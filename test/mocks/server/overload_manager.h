#pragma once

#include <string>

#include "envoy/server/overload_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class MockThreadLocalOverloadState : public ThreadLocalOverloadState {
public:
  MockThreadLocalOverloadState();
  MOCK_METHOD(const OverloadActionState&, getState, (const std::string&), (override));

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

  testing::NiceMock<MockThreadLocalOverloadState> overload_state_;
};

} // namespace Server
} // namespace Envoy
