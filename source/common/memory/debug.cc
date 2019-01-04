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
// 4 bytes are a marker (kLiveMarker or kDeadMarker1). The next 4
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

// We provide the constructor entry-point to be called to force-load the memory
// debugger and its operator-overloads, regardless of compilation mode.
Debug::Debug() {}

// We also provide the bytes-loaded counter, though this will return 0 when
// memory-debugging is not compiled in.
uint64_t Debug::bytesUsed() { return uint64_t(bytes_allocated); }

} // namespace Memory
} // namespace Envoy

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

namespace {

constexpr uint32_t kLiveMarker = 0xfeedface;        // first 4 bytes after alloc
constexpr uint32_t kDeadMarker1 = 0xabacabff;       // first 4 bytes after free
constexpr uint32_t kDeadMarker2 = 0xdeadbeef;       // overwrites the 'size' field on free
constexpr uint64_t kOverhead = 2 * sizeof(int32_t); // number of extra bytes to alloc

// Writes scribble_word over the block of memory starting at ptr and extending
// size bytes.
void scribble(void* ptr, uint64_t size, uint32_t scribble_word) {
  uint32_t num_ints = size / sizeof(uint32_t);
  uint32_t* p = static_cast<uint32_t*>(ptr);
  for (uint32_t i = 0; i < num_ints; ++i, ++p) {
    *p = scribble_word;
  }
}

// Replacement allocator, which prepends an 8-byte overhead where we write the
// size, and scribbles over the returned payload so that callers assuming
// malloced memory is 0 get data that, when interpreted as pointers, will SEGV,
// and that will be easily seen in the debugger (0xfeedface pattern).
void* debugMalloc(uint64_t size) {
  uint64_t rounded = Envoy::Memory::align(size, kOverhead);
  assert(size <= 0xffffffff); // For now we store the size in a uint32_t.
  bytes_allocated += rounded;
  uint32_t* marker = static_cast<uint32_t*>(::malloc(rounded + kOverhead));
  assert(marker != NULL);
  marker[0] = kLiveMarker;
  marker[1] = size;
  uint32_t* ret = marker + 2;
  scribble(ret, rounded, kLiveMarker);
  return reinterpret_cast<char*>(marker) + kOverhead;
}

// free() implementation corresponding to debugMalloc(), which pulls out
// the size from the 8 bytes prior to the payload, so it can know how much
// 0xdeadbeef to scribble over the freed memory before calling actual free().
void debugFree(void* ptr) {
  if (ptr != NULL) {
    char* alloced_ptr = static_cast<char*>(ptr) - kOverhead;
    uint32_t* marker = reinterpret_cast<uint32_t*>(alloced_ptr);
    uint32_t size = marker[1];
    uint64_t rounded = Envoy::Memory::align(size, kOverhead);
    bytes_allocated -= rounded;
    scribble(ptr, rounded, kDeadMarker2);
    assert(kLiveMarker == marker[0]);
    marker[0] = kDeadMarker1;
    marker[1] = kDeadMarker2;
    ::free(marker);
  }
}

} // namespace

// C++ operator new/delete overrides, in all 8 combinations:
//    (new vs delete) * (const std::nothrow_t& vs not) * ([] vs not)
// On MacOS __THROW appears to be missing so hide those in an ifdef.
#ifndef __THROW
#define __THROW
#endif

void* operator new(uint64_t size) { return debugMalloc(size); }
void operator delete(void* ptr)_GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }
void operator delete(void* ptr, size_t)_GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }

void* operator new[](size_t size) { return debugMalloc(size); }
void operator delete[](void* ptr) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }
void operator delete[](void* ptr, size_t) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }

#endif // !TCMALLOC && !ENVOY_DISABLE_MEMDEBUG
#endif // !NDEBUG
