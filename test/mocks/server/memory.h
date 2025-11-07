#pragma once

#include "envoy/server/memory.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class MockMemoryAllocatorManager : public MemoryAllocatorManager {
public:
  MockMemoryAllocatorManager();
  ~MockMemoryAllocatorManager() override;

  MOCK_METHOD(void, maybeReleaseFreeMemory, ());
  MOCK_METHOD(void, releaseFreeMemory, ());
};
} // namespace Server
} // namespace Envoy
