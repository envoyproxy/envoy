#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Server {

/**
 * Interface for managing the memory allocator.
 */
class MemoryAllocatorManager {
public:
  virtual ~MemoryAllocatorManager() = default;

  // Release free allocator memory to the OS when unused bytes exceed a configured threshold.
  virtual void maybeReleaseFreeMemory() PURE;

  // Release free allocator memory immediately (skip threshold check).
  virtual void releaseFreeMemory() PURE;
};

} // namespace Server
} // namespace Envoy
