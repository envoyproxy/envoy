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
  MOCK_METHOD(bool, draining, (Network::DrainDirection direction), (const));
  MOCK_METHOD(void, startParentShutdownSequence, ());
  MOCK_METHOD(void, startDrainSequence,
              (Network::DrainDirection direction, std::function<void()> completion));

  // Network::DrainDecision
  MOCK_METHOD(bool, drainClose, (Network::DrainDirection direction), (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addOnDrainCloseCb,
              (Network::DrainDirection direction, DrainCloseCb cb), (const, override));

  std::function<void()> drain_sequence_completion_;
  Network::DrainDirection drain_direction_;
};
} // namespace Server
} // namespace Envoy
