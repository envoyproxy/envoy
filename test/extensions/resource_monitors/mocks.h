#pragma once
#include "extensions/resource_monitors/common/memory_stats_reader.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {

class MockMemoryStatsReader : public Common::MemoryStatsReader {
public:
  MockMemoryStatsReader();

  MOCK_METHOD0(reservedHeapBytes, uint64_t());
  MOCK_METHOD0(unmappedHeapBytes, uint64_t());
  MOCK_METHOD0(allocatedHeapBytes, uint64_t());
};

} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy