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

// Centralized ifdef logic to determine whether this compile has memory
// debugging. This is exposed in the header file for testing.

// We don't run memory debugging for optimizd builds to avoid impacting
// production performance.
#ifndef NDEBUG

// We can't run memory debugging with tcmalloc due to conflicts with
// overriding operator new/delete. Note tcmalloc allows installation
// of a malloc hook (MallocHook::AddNewHook(&tcmallocHook)) e.g.
// tcmallocHook(const void* ptr, size_t size). I tried const_casting ptr
// and scribbling over it, but this results in a SEGV in grpc and the
// internals of gtest.
//
// And in any case, you can't use the tcmalloc hooks to do free-scribbling
// as it does not pass in the size to the corresponding free hook. See
// gperftools/malloc_hook.h for details.
//
// We also must disable memory debugging for tsan/asan builds, as they also
// need to override operator new/delete.
#if !defined(TCMALLOC) && !defined(ENVOY_DISABLE_MEMDEBUG)

#define MEMORY_DEBUG_ENABLED 1

#endif // !TCMALLOC && !ENVOY_DISABLE_MEMDEBUG
#endif // !NDEBUG

} // namespace Memory
} // namespace Envoy
