#include "drain_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::SaveArg;

MockDrainManager::MockDrainManager() {
  // force `draining_ = true` when drain starts
  ON_CALL(*this, startDrainSequence(_, _))
      .WillByDefault(
          Invoke([this](Network::DrainDirection direction, std::function<void()> completion) {
            drain_direction_ = direction;
            drain_sequence_completion_ = completion;
            draining_ = true;
          }));

  // Return draining_ state for draining() and drainClose() by default
  ON_CALL(*this, draining(_)).WillByDefault(Invoke([this](Network::DrainDirection) { return draining_; }));
  ON_CALL(*this, drainClose(_)).WillByDefault(Invoke([this](Network::DrainDirection) { return draining_; }));

  // instantiate a mock drain manager when createChildManager() is called
  ON_CALL(*this, createChildManager(_))
      .WillByDefault(Invoke([](Event::Dispatcher&) -> DrainManagerPtr {
        return std::make_unique<NiceMock<MockDrainManager>>();
      }));
  ON_CALL(*this, createChildManager(_, _))
      .WillByDefault(
          Invoke([](Event::Dispatcher&, envoy::config::listener::v3::Listener::DrainType) -> DrainManagerPtr {
            return std::make_unique<NiceMock<MockDrainManager>>();
          }));
}

MockDrainManager::~MockDrainManager() = default;

} // namespace Server
} // namespace Envoy
