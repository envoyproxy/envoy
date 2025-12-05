#include "test/mocks/server/memory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::Return;

MockMemoryAllocatorManager::MockMemoryAllocatorManager() {
  ON_CALL(*this, maybeReleaseFreeMemory).WillByDefault(Return());
  ON_CALL(*this, releaseFreeMemory).WillByDefault(Return());
}

MockMemoryAllocatorManager::~MockMemoryAllocatorManager() = default;

} // namespace Server
} // namespace Envoy
