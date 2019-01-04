#pragma once

#include <cassert>
#include <cstdint>

namespace Envoy {
namespace Memory {

inline uint64_t align(uint64_t size, uint64_t alignment) {
  // Check that alignment is a power of 2:
  // http://www.graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
  assert((alignment > 0) && ((alignment & (alignment - 1)) == 0));
  return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace Memory
} // namespace Envoy
