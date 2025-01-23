#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/server/drain_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager() override;

  // Server::DrainManager
  MOCK_METHOD(bool, drainClose, (Network::DrainDirection direction), (const));
  MOCK_METHOD(bool, draining, (Network::DrainDirection direction), (const));
  MOCK_METHOD(void, startDrainSequence,
              (Network::DrainDirection direction, std::function<void()> completion));
  MOCK_METHOD(void, startParentShutdownSequence, ());

  std::function<void()> drain_sequence_completion_;
  Network::DrainDirection drain_direction_;
};
} // namespace Server
} // namespace Envoy
