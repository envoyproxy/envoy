#pragma once

#include "envoy/server/watchdog.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockWatchDog : public WatchDog {
public:
  MockWatchDog();
  ~MockWatchDog() override;

  // Server::WatchDog
  MOCK_METHOD(void, touch, (), (override));
  MOCK_METHOD(Thread::ThreadId, threadId, (), (const, override));
};
} // namespace Server
} // namespace Envoy
