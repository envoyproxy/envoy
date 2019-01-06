#pragma once

#include <cstdint>

namespace Envoy {
namespace Memory {

class Debug {
public:
  // Instantiate to force-load the memory debugging module. This is called
  // whether or not memory-debugging is enabled, which is controlled by ifdefs
  // in mem_debug.cc.
  Debug();

  // Returns the number of bytes used -- if memory debugging is enabled.
  // Otherwise returns 0.
  static uint64_t bytesUsed();
};

} // namespace Memory
} // namespace Envoy
