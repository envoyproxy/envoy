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
  MOCK_METHOD(void, startWatchdog, (Event::Dispatcher & dispatcher));
  MOCK_METHOD(void, touch, ());
  MOCK_METHOD(Thread::ThreadId, threadId, (), (const));
  MOCK_METHOD(MonotonicTime, lastTouchTime, (), (const));
};
} // namespace Server
} // namespace Envoy
