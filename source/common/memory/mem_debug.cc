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
// Note: this memory debugging will interfere with Valgrind's ability to
// detect read-before-write errors, and hence should be disabled if you
// want to run with valgrind. This can be detected automatically using
// macros from valgrind.h, but then that would *require* valgrind be installed
// before building mod_pagespeed. See
//
// http://valgrind.org/docs/manual/manual-core-adv.html#manual-core-adv.clientreq

// This code was adapted from mod_pagespeed, and adapted for Envoy
// style. Original source:
// https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/base/mem_debug.cc

#define INSTALL_HOOKS

// We don't run memory debugging for optimizd builds to avoid impacting
// production performance.
#ifndef NDEBUG

#include <cstdlib>

#include "common/common/assert.h"

namespace {

constexpr int32_t kLiveMarker = 0xfeedface;       // first 4 bytes after alloc

void scribble(void* ptr, size_t size, int32_t scribble_word) {
  int32_t num_ints = size / sizeof(int32_t);
  int32_t* p = static_cast<int32_t*>(ptr);
  for (int i = 0; i < num_ints; ++i, ++p) {
    *p = scribble_word;
  }
}

} // namespace

// We can't run full memory debugging with tcmalloc due to conflicts with
// overriding operator new/delete. However we *can* run the pre-allocation
// scribbling via a hook. We can't post-allocate scribble because the
// tcmalloc new/delete hooks do not provide the size.
#ifdef TCMALLOC

#include "gperftools/malloc_hook.h"

namespace {

void tcmallocScribbleHook(const void* ptr, size_t size) {
  scribble(const_cast<void*>(ptr), size, kLiveMarker);
}

} // namespace

#undef INSTALL_HOOKS
#define INSTALL_HOOKS { \
  static bool init = false; \
  if (!init) { \
    init = true; \
    MallocHook::AddNewHook(&tcmallocScribbleHook); \
  } \
}

#else

namespace {

constexpr int32_t kDeadMarker1 = 0xabacabff;      // first 4 bytes after free
constexpr int32_t kDeadMarker2 = 0xdeadbeef;      // overwrites the 'size' field on free
constexpr size_t kOverhead = 2 * sizeof(int32_t); // number of extra bytes to alloc

size_t roundedSize(size_t size) {
  if (size == 0) {
    size = kOverhead;
  } else if ((size % kOverhead) != 0) {
    size = size + kOverhead - (size % kOverhead);
  }
  return size;
}

void* debugMalloc(size_t size) {
  size_t rounded = roundedSize(size);
  int32_t* marker = static_cast<int32_t*>(malloc(rounded + kOverhead));
  ASSERT(marker != NULL);
  marker[0] = kLiveMarker;
  marker[1] = size;
  int32_t* ret = marker + 2;
  scribble(ret, rounded, kLiveMarker);
  return reinterpret_cast<char*>(marker) + kOverhead;
}

void debugFree(void* ptr) {
  if (ptr != NULL) {
    char* alloced_ptr = static_cast<char*>(ptr) - kOverhead;
    int32_t* marker = reinterpret_cast<int32_t*>(alloced_ptr);
    scribble(ptr, roundedSize(marker[1]), kDeadMarker2);
    ASSERT(kLiveMarker == marker[0]);
    marker[0] = kDeadMarker1;
    marker[1] = kDeadMarker2;
    free(marker);
  }
}

} // namespace

// C++ operator new/delete overrides, in all 8 combinations:
//    (new vs delete) * (const std::nothrow_t& vs not) * ([] vs not)
// On MacOS __THROW appears to be missing so hide those in an ifdef.
#ifndef __THROW
#define __THROW
#endif

void* operator new(size_t size) { return debugMalloc(size); }
void operator delete(void* ptr) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }
void operator delete(void* ptr, size_t) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }

void* operator new[](size_t size) { return debugMalloc(size); }
void operator delete[](void* ptr) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }
void operator delete[](void* ptr, size_t) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }

//void* operator new(size_t size, const std::nothrow_t&) __THROW { return debugMalloc(size); }
//void operator delete(void* ptr) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }
//void operator delete(void* ptr, const std::nothrow_t&)__THROW { debugFree(ptr); }
//void* operator new[](size_t size, const std::nothrow_t&) __THROW { return debugMalloc(size); }
//void operator delete[](void* ptr) _GLIBCXX_USE_NOEXCEPT { debugFree(ptr); }
//void operator delete[](void* ptr, const std::nothrow_t&) __THROW { debugFree(ptr); }

#endif // TCMALLOC
#endif // !NDEBUG

// We provide the entry-point to be called to force-load the memory debugger
// regardless of compilation mode.
namespace Envoy {

void MemDebugLoader() {
  INSTALL_HOOKS
}

} // namespace Envoy
