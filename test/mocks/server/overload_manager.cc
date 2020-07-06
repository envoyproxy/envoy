#include "overload_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::ReturnRef;
MockOverloadManager::MockOverloadManager() {
  ON_CALL(*this, getThreadLocalOverloadState()).WillByDefault(ReturnRef(overload_state_));
}

MockOverloadManager::~MockOverloadManager() = default;

} // namespace Server
} // namespace Envoy
