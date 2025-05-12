#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnPointee;
using testing::SaveArg;

namespace Envoy {
namespace Event {

MockProvisionalDispatcher::MockProvisionalDispatcher() {
  ON_CALL(*this, timeSource()).WillByDefault(testing::ReturnRef(time_system_));
}
} // namespace Event
} // namespace Envoy
