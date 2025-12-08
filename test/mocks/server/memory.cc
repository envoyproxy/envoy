#include "test/mocks/server/memory.h"

namespace Envoy {
namespace Server {

MockMemoryAllocatorManager::MockMemoryAllocatorManager() {
  ON_CALL(*this, maybeReleaseFreeMemory).WillByDefault(testing::Return());
  ON_CALL(*this, releaseFreeMemory).WillByDefault(testing::Return());
}

MockMemoryAllocatorManager::~MockMemoryAllocatorManager() = default;

} // namespace Server
} // namespace Envoy
