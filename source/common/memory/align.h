#pragma once

#include <cstdint>
#include <type_traits>

namespace Envoy {
namespace Memory {

template <uint64_t alignment> inline uint64_t align(uint64_t size) {
  // Check that alignment is a power of 2:
  // http://www.graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
  static_assert((alignment > 0) && ((alignment & (alignment - 1)) == 0),
                "alignment must be a power of 2");
  return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace Memory
} // namespace Envoy
