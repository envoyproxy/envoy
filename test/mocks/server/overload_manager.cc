#include "test/mocks/server/overload_manager.h"

#include <string>

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::NiceMock;
using ::testing::ReturnNew;
using ::testing::ReturnRef;

MockThreadLocalOverloadState::MockThreadLocalOverloadState()
    : disabled_state_(OverloadActionState::inactive()) {
  ON_CALL(*this, getState).WillByDefault(ReturnRef(disabled_state_));
}

MockOverloadManager::MockOverloadManager() {
  ON_CALL(*this, getThreadLocalOverloadState()).WillByDefault(ReturnRef(overload_state_));
}

MockOverloadManager::~MockOverloadManager() = default;

} // namespace Server
} // namespace Envoy
