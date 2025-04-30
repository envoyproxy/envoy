#include "drain_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::DoAll;
using ::testing::SaveArg;

MockDrainManager::MockDrainManager() {
  ON_CALL(*this, startDrainSequence(_, _))
      .WillByDefault(DoAll(SaveArg<0>(&drain_direction_), SaveArg<1>(&drain_sequence_completion_)));
}

MockDrainManager::~MockDrainManager() = default;

} // namespace Server
} // namespace Envoy
