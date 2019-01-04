#pragma once

#include <cstdint>

namespace Envoy {
namespace Memory {

class Debug {
public:
  // Called to force-load the memory debugging module. This is called whether
  // or not memory-debugging is enabled, which is controlled by ifdefs in
  // mem_debug.cc.
  Debug();
  static uint64_t bytesUsed();
};

} // namespace Memory
} // namespace Envoy
