#pragma once

#include "source/common/http/session_idle_list.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockSessionIdleList : public Http::SessionIdleListInterface {
public:
  MockSessionIdleList() = default;
  ~MockSessionIdleList() override = default;

  MOCK_METHOD(void, AddSession, (Http::IdleSessionInterface & session), (override));
  MOCK_METHOD(void, RemoveSession, (Http::IdleSessionInterface & session), (override));
  MOCK_METHOD(void, MaybeTerminateIdleSessions, (bool is_saturated), (override));
};

} // namespace Http
} // namespace Envoy
