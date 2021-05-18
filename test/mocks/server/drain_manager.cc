#include "drain_manager.h"

#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/server/drain_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::Invoke;
using ::testing::SaveArg;

MockDrainManager::MockDrainManager() {
  ON_CALL(*this, startDrainSequence(_)).WillByDefault(SaveArg<0>(&drain_sequence_completion_));

  ON_CALL(*this, createChildManager(_, _))
      .WillByDefault(
          Invoke([](Event::Dispatcher&,
                    envoy::config::listener::v3::Listener_DrainType) -> DrainManagerSharedPtr {
            return std::shared_ptr<DrainManager>(new MockDrainManager());
          }));
  ON_CALL(*this, createChildManager(_))
      .WillByDefault(Invoke([](Event::Dispatcher&) -> DrainManagerSharedPtr {
        return std::shared_ptr<DrainManager>(new MockDrainManager());
      }));
}

MockDrainManager::~MockDrainManager() = default;

} // namespace Server
} // namespace Envoy
