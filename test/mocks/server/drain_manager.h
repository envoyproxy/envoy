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

using ::testing::NiceMock;

namespace Envoy {
namespace Server {
class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager() override;

  // Network::DrainManager
  MOCK_METHOD(DrainManagerSharedPtr, createChildManager,
              (Event::Dispatcher&, envoy::config::listener::v3::Listener::DrainType), (override));
  MOCK_METHOD(DrainManagerSharedPtr, createChildManager, (Event::Dispatcher&), (override));

  MOCK_METHOD(bool, drainClose, (), (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addOnDrainCloseCb, (DrainCloseCb cb), (const, override));

  // Server::DrainManager
  MOCK_METHOD(bool, draining, (), (const));
  MOCK_METHOD(void, startParentShutdownSequence, ());
  MOCK_METHOD(void, _startDrainSequence, (std::function<void()> completion));
  void startDrainSequence(std::function<void()> cb) override;

  /**
   * Apply some setup/configuration to this drain manager and to all child drain-managers created
   * through the `createChildManager` methods.
   */
  void applyAllSetup(std::function<void(MockDrainManager&)> setup) {
    setup(*this);
    applyChildSetup(setup);
  }

  /**
   * Apply some setup/configuration to all drain-managers created through `createChildManager`
   */
  void applyChildSetup(std::function<void(MockDrainManager&)> setup) { child_setup_ = setup; }

  std::function<void(MockDrainManager&)> child_setup_ = [](MockDrainManager&) {};
  std::function<void()> drain_sequence_completion_;

  /**
   * All children created by calls to `createChildManager`
   */
  std::vector<std::weak_ptr<DrainManager>> children_{};

  std::atomic<bool> draining_{false};
};
} // namespace Server
} // namespace Envoy
