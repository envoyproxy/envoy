#include "test/mocks/server/overload_manager.h"

#include <string>

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::Return;
using ::testing::ReturnRef;

MockThreadLocalOverloadState::MockThreadLocalOverloadState()
    : disabled_state_(OverloadActionState::inactive()) {
  ON_CALL(*this, getState).WillByDefault(ReturnRef(disabled_state_));
  ON_CALL(*this, tryAllocateResource).WillByDefault(Return(true));
  ON_CALL(*this, tryDeallocateResource).WillByDefault(Return(true));
  ON_CALL(*this, isResourceMonitorEnabled).WillByDefault(Return(false));
  ON_CALL(*this, getProactiveResourceMonitorForTest)
      .WillByDefault(Return(makeOptRefFromPtr<ProactiveResourceMonitor>(nullptr)));
}

MockOverloadManager::MockOverloadManager() {
  ON_CALL(*this, getThreadLocalOverloadState()).WillByDefault(ReturnRef(overload_state_));
}

MockOverloadManager::~MockOverloadManager() = default;

} // namespace Server
} // namespace Envoy
