#include "source/common/event/dispatcher_impl.h"
#include "source/common/memory/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"

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

class MemoryReleaseTest : public testing::Test {
protected:
  void initialiseAllocator(const uint64_t background_release_rate) {
    memory_allocator_ = std::make_unique<Memory::Allocator>(Thread::threadFactoryForTest(),
                                                            background_release_rate);
  }

  std::unique_ptr<Memory::Allocator> memory_allocator_;
};

TEST_F(MemoryReleaseTest, RelaseRateZeroNoRelease) {
  auto ptr = std::make_unique<MyStruct>();
  uint64_t before = Stats::totalCurrentlyAllocated();
  EXPECT_LOG_NOT_CONTAINS("info",
                          "Configured tcmalloc with background release rate: 0 bytes per second",
                          initialiseAllocator(0 /*bytes per second*/));
  uint64_t after = Stats::totalCurrentlyAllocated();
  EXPECT_GE(sizeof(MyStruct), after - before);
}

TEST_F(MemoryReleaseTest, RelaseRateAboveZeroMemoryReleased) {
  auto ptr = std::make_unique<MyStruct>();
  uint64_t before = Stats::totalCurrentlyAllocated();
  EXPECT_LOG_CONTAINS("info",
                      "Configured tcmalloc with background release rate: 512 bytes per second",
                      initialiseAllocator(512 /*bytes per second*/));
  uint64_t after = Stats::totalCurrentlyAllocated();
  EXPECT_LT(sizeof(MyStruct), after - before);
}

#if defined(__APPLE__)

TEST_F(MemoryReleaseTest, UnsupportedPlatformZeroMemoryReleased) {
  auto ptr = std::make_unique<MyStruct>();
  uint64_t before = Stats::totalCurrentlyAllocated();
  EXPECT_LOG_CONTAINS("info", "Current platform does not suport tcmalloc background actions",
                      initialiseAllocator(512 /*bytes per second*/));
  uint64_t after = Stats::totalCurrentlyAllocated();
  EXPECT_GE(sizeof(MyStruct), after - before);
}

#endif //__APPLE__

#endif // ENVOY_MEMORY_DEBUG_ENABLED

} // namespace
} // namespace Memory
} // namespace Envoy
