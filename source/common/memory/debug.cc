// Very simple memory debugging overrides for operator new/delete, to
// help us quickly find simple memory violations:
//   1. Double destruct
//   2. Read before write (via scribbling)
//   3. Read after delete (via scribbling)
//
// Note that valgrind does all of this much better, but is too slow to run all
// the time. asan does read-after-delete detection but not read-before-init
// detection. See
// https://clang.llvm.org/docs/AddressSanitizer.html#initialization-order-checking
// for more details.

// Principle of operation: add 8 bytes to every allocation. The first
// 4 bytes are a marker (LiveMarker1 or DeadMarker1). The next 4
// bytes are used to store size of the allocation, which helps us
// know how many bytes to scribble when we free.
//
// This code was adapted from mod_pagespeed, and adapted for Envoy
// style. Original source:
// https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/base/mem_debug.cc

// We keep a global count of bytes allocated so that memory-consumption tests
// work with memory debugging. Put another way, if we disable tcmalloc when
// compiling for debug, we want the memory-debugging tests to work, otherwise we
// can't debug them.
#include "common/memory/debug.h"

#include <atomic>
#include <cassert> // don't use Envoy ASSERT as it may allocate memory.
#include <cstdlib>

#include "common/memory/align.h"

static std::atomic<int64_t> bytes_allocated(0);

namespace Envoy {
namespace Memory {

// We always provide the constructor entry-point to be called to force-load this
// module, regardless of compilation mode. If the #ifdefs line as required
// below, then it will also override operator new/delete in various flavors so
// that we can debug memory issues.
Debug::Debug() = default;

// We also provide the bytes-loaded counter, though this will return 0 when
// memory-debugging is not compiled in.
uint64_t Debug::bytesUsed() { return uint64_t(bytes_allocated); }

} // namespace Memory
} // namespace Envoy

#ifdef MEMORY_DEBUG_ENABLED

namespace {

constexpr uint32_t LiveMarker1 = 0xfeedface;         // first 4 bytes after alloc
constexpr uint64_t LiveMarker2 = 0xfeedfacefeedface; // first 4 bytes after alloc
constexpr uint32_t DeadMarker1 = 0xabacabff;         // first 4 bytes after free
constexpr uint64_t DeadMarker2 = 0xdeadbeefdeadbeef; // overwrites the 'size' field on free
constexpr uint64_t Overhead = sizeof(uint64_t);      // number of extra bytes to alloc

// Writes scribble_word over the block of memory starting at ptr and extending
// size bytes.
inline void scribble(void* ptr, uint64_t rounded_size, uint64_t scribble_word) {
  assert((rounded_size % Overhead) == 0);
  uint64_t num_uint64s = rounded_size / sizeof(uint64_t);
  uint64_t* p = static_cast<uint64_t*>(ptr);
  for (uint64_t i = 0; i < num_uint64s; ++i, ++p) {
    *p = scribble_word;
  }
}

// Replacement allocator, which prepends an 8-byte overhead where we write the
// size, and scribbles over the returned payload so that callers assuming
// malloced memory is 0 get data that, when interpreted as pointers, will SEGV,
// and that will be easily seen in the debugger (0xfeedface pattern).
void* debugMalloc(uint64_t size) {
  assert(size <= 0xffffffff); // For now we store the original size in a uint32_t.
  uint64_t rounded = Envoy::Memory::align(size, Overhead);
  bytes_allocated += rounded;
  uint32_t* marker = static_cast<uint32_t*>(::malloc(rounded + Overhead));
  assert(marker != NULL);
  marker[0] = LiveMarker1;
  marker[1] = size;
  uint32_t* ret = marker + 2;
  scribble(ret, rounded, LiveMarker2);
  return reinterpret_cast<char*>(marker) + Overhead;
}

// free() implementation corresponding to debugMalloc(), which pulls out
// The size from the 8 bytes prior to the payload, so it can know how much
// 0xdeadbeef to scribble over the freed memory before calling actual free().
void debugFree(void* ptr) {
  if (ptr != NULL) {
    char* alloced_ptr = static_cast<char*>(ptr) - Overhead;
    uint32_t* marker = reinterpret_cast<uint32_t*>(alloced_ptr);
    uint32_t size = marker[1];
    uint64_t rounded = Envoy::Memory::align(size, Overhead);
    bytes_allocated -= rounded;
    scribble(ptr, rounded, DeadMarker2);
    assert(LiveMarker1 == marker[0]);
    marker[0] = DeadMarker1;
    marker[1] = DeadMarker2 & 0xffffffff;
    ::free(marker);
  }
}

} // namespace

void* operator new(uint64_t size) { return debugMalloc(size); }
void operator delete(void* ptr) noexcept { debugFree(ptr); }
void operator delete(void* ptr, size_t) noexcept { debugFree(ptr); }

void* operator new[](size_t size) { return debugMalloc(size); }
void operator delete[](void* ptr) noexcept { debugFree(ptr); }
void operator delete[](void* ptr, size_t) noexcept { debugFree(ptr); }

#endif // MEMORY_DEBUG_ENABLED
