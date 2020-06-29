#include "guard_dog.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
MockGuardDog::MockGuardDog() : watch_dog_(new NiceMock<MockWatchDog>()) {
  ON_CALL(*this, createWatchDog(_, _)).WillByDefault(Return(watch_dog_));
}

MockGuardDog::~MockGuardDog() = default;

} // namespace Server

} // namespace Envoy
