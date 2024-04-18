#include "drain_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::SaveArg;

MockDrainManager::MockDrainManager() {
  ON_CALL(*this, startDrainSequence(_)).WillByDefault(SaveArg<0>(&drain_sequence_completion_));
}

MockDrainManager::~MockDrainManager() = default;

} // namespace Server
} // namespace Envoy
