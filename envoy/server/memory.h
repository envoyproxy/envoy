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

  /**
   * Releases free memory if the memory is above the threshold.
   */
  virtual void maybeReleaseFreeMemory() PURE;

  /**
   * Releases free memory immediately.
   */
  virtual void releaseFreeMemory() PURE;
};

} // namespace Server
} // namespace Envoy
