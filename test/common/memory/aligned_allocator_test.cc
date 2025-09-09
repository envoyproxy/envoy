#include <array>
#include <vector>

#include "source/common/memory/aligned_allocator.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Memory {
namespace {

TEST(AlignedAllocatorTest, AllocationSize) {
  using Alloc = AlignedAllocator<uint8_t, 8>;
  EXPECT_EQ(Alloc::round_up_to_alignment(0), 0);
  EXPECT_EQ(Alloc::round_up_to_alignment(1), 8);
  EXPECT_EQ(Alloc::round_up_to_alignment(2), 8);
  EXPECT_EQ(Alloc::round_up_to_alignment(4), 8);
  EXPECT_EQ(Alloc::round_up_to_alignment(7), 8);
  EXPECT_EQ(Alloc::round_up_to_alignment(8), 8);
  EXPECT_EQ(Alloc::round_up_to_alignment(9), 16);
  EXPECT_EQ(Alloc::round_up_to_alignment(12), 16);
  EXPECT_EQ(Alloc::round_up_to_alignment(15), 16);
  EXPECT_EQ(Alloc::round_up_to_alignment(16), 16);
  EXPECT_EQ(Alloc::round_up_to_alignment(17), 24);
}

TEST(AlignedAllocatorTest, Allocation) {
  AlignedAllocator<uint8_t, alignof(void*)> alloc;
  for (int i = 0; i < 1000; ++i) {
    const std::size_t n = i + 1;
    uint8_t* p = alloc.allocate(n);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignof(void*), 0);
    alloc.deallocate(p, n);
  }
}

TEST(AlignedAllocatorTest, AllocationInVector) {
  using AlignedVector = std::vector<uint8_t, AlignedAllocator<uint8_t, alignof(void*)>>;
  for (int i = 0; i < 1000; ++i) {
    std::array<char, 4> a;
    AlignedVector v(a.begin(), a.end());
    EXPECT_EQ(reinterpret_cast<uintptr_t>(v.data()) % alignof(void*), 0);
  }
}

TEST(AlignedAllocatorTest, Nullability) {
  AlignedAllocator<uint8_t, alignof(void*)> alloc;
  EXPECT_EQ(alloc.allocate(0), nullptr);
  alloc.deallocate(nullptr, 0); // Should not crash
}

} // namespace
} // namespace Memory
} // namespace Envoy
