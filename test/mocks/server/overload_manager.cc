#include "overload_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::ReturnRef;

MockThreadLocalOverloadState::MockThreadLocalOverloadState()
    : disabled_state_(OverloadActionState::Inactive) {
  ON_CALL(*this, getState).WillByDefault(ReturnRef(disabled_state_));
}

MockOverloadManager::MockOverloadManager() {
  ON_CALL(*this, getThreadLocalOverloadState()).WillByDefault(ReturnRef(overload_state_));
}

MockOverloadManager::~MockOverloadManager() = default;

} // namespace Server
} // namespace Envoy
