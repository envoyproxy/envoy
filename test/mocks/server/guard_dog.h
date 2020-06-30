#pragma once

#include "envoy/server/guarddog.h"

#include "gmock/gmock.h"
#include "watch_dog.h"

namespace Envoy {
namespace Server {
class MockGuardDog : public GuardDog {
public:
  MockGuardDog();
  ~MockGuardDog() override;

  // Server::GuardDog
  MOCK_METHOD(WatchDogSharedPtr, createWatchDog,
              (Thread::ThreadId thread_id, const std::string& thread_name));
  MOCK_METHOD(void, stopWatching, (WatchDogSharedPtr wd));

  std::shared_ptr<MockWatchDog> watch_dog_;
};
} // namespace Server
} // namespace Envoy
