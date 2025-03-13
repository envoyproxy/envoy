#pragma once

#if defined(__ANDROID_API__) && __ANDROID_API__ < 28
#include <stdlib.h>

#define ALIGNED_ALLOCATOR_USE_POSIX_MEMALIGN 1
#endif // if defined(__ANDROID_API__) && __ANDROID_API__ < 28

#include <cstddef>
#include <cstdlib>

namespace Envoy {
namespace Memory {

// Custom allocator using std::aligned_alloc to allocate |T|s at |Alignment|.
template <typename T, std::size_t Alignment> class AlignedAllocator {
public:
  static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be a power of 2");
#ifdef ALIGNED_ALLOCATOR_USE_POSIX_MEMALIGN
  static_assert((Alignment % sizeof(void*)) == 0,
                "Alignment must be a multiple of sizeof(void*) when using posix_memalign");
#endif
  using value_type = T;

  AlignedAllocator() noexcept = default;

  // Copy constructor for rebind compatibility.
  template <typename U> explicit AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  static std::size_t round_up_to_alignment(std::size_t bytes) {
    return (bytes + Alignment - 1) & ~(Alignment - 1);
  }

  // Allocate aligned memory.
  // This never throws std::bad_alloc, it returns nullptr on failure.
  T* allocate(std::size_t n) {
    // STL containers never call allocate with n=0.
    if (n == 0) {
      return nullptr;
    }
    std::size_t bytes = n * sizeof(T);
#ifdef ALIGNED_ALLOCATOR_USE_POSIX_MEMALIGN
    void* ptr = nullptr;
    if (posix_memalign(&ptr, Alignment, bytes) != 0) {
      return nullptr;
    }
    return static_cast<T*>(ptr);
#else
    // Ensure bytes is a multiple of Alignment, which is required by std::aligned_alloc.
    bytes = round_up_to_alignment(bytes);
    return static_cast<T*>(std::aligned_alloc(Alignment, bytes));
#endif
  }

  void deallocate(T* p, std::size_t) noexcept {
    if (p != nullptr) {
#ifdef ALIGNED_ALLOCATOR_USE_POSIX_MEMALIGN
      free(p);
#else
      std::free(p);
#endif
    }
  }

  // Equality operators (required for allocator_traits)
  template <typename U> bool operator==(const AlignedAllocator<U, Alignment>&) const noexcept {
    return true;
  }

  template <typename U> bool operator!=(const AlignedAllocator<U, Alignment>&) const noexcept {
    return false;
  }

  // Satisfy libc++ requirement.
  template <typename U> struct rebind { using other = AlignedAllocator<U, Alignment>; };
};

} // namespace Memory
} // namespace Envoy
