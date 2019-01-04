#pragma once

#include <cstdint>

namespace Envoy {

// Called to force-load the memory debugging module. This is called whether
// or not memory-debugging is enabled, which is controlled by ifdefs in
// mem_debug.cc.
void MemDebugLoader();
uint64_t MemDebugMemoryUsed();

} // namespace Envoy
