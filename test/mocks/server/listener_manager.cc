#include "listener_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Server {

MockListenerManager::MockListenerManager() {
  ON_CALL(*this, addOrUpdateListener(_, _, _)).WillByDefault(Return(false));
}

MockListenerManager::~MockListenerManager() = default;

} // namespace Server
} // namespace Envoy
