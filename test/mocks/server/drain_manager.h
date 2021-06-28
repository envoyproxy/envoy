#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

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
  MOCK_METHOD(DrainManagerPtr, createChildManager,
              (Event::Dispatcher&, envoy::config::listener::v3::Listener::DrainType), (override));
  MOCK_METHOD(DrainManagerPtr, createChildManager, (Event::Dispatcher&), (override));
  MOCK_METHOD(bool, draining, (), (const));
  MOCK_METHOD(void, startParentShutdownSequence, ());
  MOCK_METHOD(void, startDrainSequence, (std::function<void()> completion));

  // Network::DrainDecision
  MOCK_METHOD(bool, drainClose, (), (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addOnDrainCloseCb, (DrainCloseCb cb), (const, override));

  std::function<void()> drain_sequence_completion_;
};
} // namespace Server
} // namespace Envoy
