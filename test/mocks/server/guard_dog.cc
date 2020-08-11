#include "guard_dog.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

MockGuardDog::MockGuardDog() : watch_dog_(new NiceMock<MockWatchDog>()) {
  ON_CALL(*this, createWatchDog(_, _)).WillByDefault(Return(watch_dog_));
}

MockGuardDog::~MockGuardDog() = default;

} // namespace Server
} // namespace Envoy
