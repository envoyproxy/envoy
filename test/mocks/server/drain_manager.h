#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/server/drain_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager() override;

  // Network::DrainManager
  MOCK_METHOD(bool, drainClose, (), (const));
  MOCK_METHOD(Common::ThreadSafeCallbackHandlePtr, addOnDrainCloseCb,
              (Event::Dispatcher & dispatcher, DrainCloseCb cb), (const, override));

  // Server::DrainManager
  MOCK_METHOD(bool, draining, (), (const));
  MOCK_METHOD(void, startDrainSequence, (std::function<void()> completion));
  MOCK_METHOD(void, startParentShutdownSequence, ());

  std::function<void()> drain_sequence_completion_;
};
} // namespace Server
} // namespace Envoy
