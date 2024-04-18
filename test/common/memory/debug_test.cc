#include "source/common/memory/stats.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Memory {
namespace {

#ifdef ENVOY_MEMORY_DEBUG_ENABLED

constexpr int ArraySize = 10;

struct MyStruct {
  MyStruct() : x_(0) {} // words_ is uninitialized; will have whatever allocator left there.
  uint64_t x_;
  uint64_t words_[ArraySize];
};

TEST(MemoryDebug, ByteSize) {
  uint64_t before = Stats::totalCurrentlyAllocated();
  auto ptr = std::make_unique<MyStruct>();
  uint64_t after = Stats::totalCurrentlyAllocated();
  EXPECT_LE(sizeof(MyStruct), after - before);
}

TEST(MemoryDebug, ScribbleOnNew) {
  auto ptr = std::make_unique<MyStruct>();
  for (int i = 0; i < ArraySize; ++i) {
    // This is the pattern written by tcmalloc's debug library.
    EXPECT_EQ(0xabababababababab, ptr->words_[i]);
  }
}

TEST(MemoryDebug, ScribbleOnDelete) {
  uint64_t* words;
  {
    auto ptr = std::make_unique<MyStruct>();
    words = ptr->words_;
  }
  for (int i = 0; i < ArraySize; ++i) {
    // This is the pattern written by tcmalloc's debug library on destruction.
    // Note: this test cannot be run under valgrind or asan.
    EXPECT_EQ(0xcdcdcdcdcdcdcdcd, words[i]);
  }
}

TEST(MemoryDebug, ZeroByteAlloc) { auto ptr = std::make_unique<uint8_t[]>(0); }

#endif // ENVOY_MEMORY_DEBUG_ENABLED

} // namespace
} // namespace Memory
} // namespace Envoy
