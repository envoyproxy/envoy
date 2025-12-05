#include "source/common/event/dispatcher_impl.h"
#include "source/common/memory/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Memory {

class AllocatorManagerPeer {
public:
  static std::chrono::milliseconds
  memoryReleaseInterval(const AllocatorManager& allocator_manager) {
    return allocator_manager.memory_release_interval_msec_;
  }
  static uint64_t bytesToRelease(const AllocatorManager& allocator_manager) {
    return allocator_manager.bytes_to_release_;
  }
  static uint64_t minimumUnfreedMemoryBytesThreshold(const AllocatorManager& allocator_manager) {
    return allocator_manager.minimum_unfreed_memory_bytes_threshold_;
  }
};

namespace {

static const int MB = 1048576;

class MemoryReleaseTest : public testing::Test {
protected:
  MemoryReleaseTest()
      : api_(Api::createApiForTest(stats_, time_system_)),
        dispatcher_("test_thread", *api_, time_system_), scope_("memory_release_test.", stats_) {}

  void initialiseAllocatorManager(uint64_t bytes_to_release, float release_interval_s = 0,
                                  uint64_t unfreed_memory_bytes_threshold = 0) {
    std::string yaml_config = fmt::format("bytes_to_release: {}\n", bytes_to_release);

    if (release_interval_s > 0) {
      yaml_config += fmt::format("memory_release_interval: {}s\n", release_interval_s);
    }

    if (unfreed_memory_bytes_threshold > 0) {
      yaml_config += fmt::format("minimum_unfreed_memory_bytes_threshold: {}\n",
                                 unfreed_memory_bytes_threshold);
    }

    const auto proto_config =
        TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
    allocator_manager_ = std::make_unique<Memory::AllocatorManager>(*api_, scope_, proto_config);
  }

  void step(const std::chrono::milliseconds& step) { time_system_.advanceTimeWait(step); }

  Envoy::Stats::TestUtil::TestStore stats_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherImpl dispatcher_;
  Envoy::Stats::TestUtil::TestScope scope_;
  std::unique_ptr<Memory::AllocatorManager> allocator_manager_;
};

TEST_F(MemoryReleaseTest, ReleaseRateAboveZeroDefaultIntervalMemoryReleased) {
  size_t initial_allocated_bytes = Stats::totalCurrentlyAllocated();
  auto a = std::make_unique<unsigned char[]>(MB);
  auto b = std::make_unique<unsigned char[]>(MB);
  if (Stats::totalCurrentlyAllocated() <= initial_allocated_bytes) {
    GTEST_SKIP() << "Skipping test, cannot measure memory usage precisely on this platform.";
  }
#if defined(GPERFTOOLS_TCMALLOC)
  EXPECT_LOG_CONTAINS("error",
                      "Memory releasing is not supported for gperf tcmalloc, no memory releasing "
                      "will be configured.",
                      initialiseAllocatorManager(MB /*bytes per second*/, 0));
#elif defined(TCMALLOC)
  auto initial_unmapped_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_CONTAINS(
      "info",
      "Configured tcmalloc with background release rate: 1048576 bytes per 1000 milliseconds",
      initialiseAllocatorManager(MB /*bytes per second*/, 0));
  EXPECT_EQ(MB, AllocatorManagerPeer::bytesToRelease(*allocator_manager_));
  EXPECT_EQ(std::chrono::milliseconds(1000),
            AllocatorManagerPeer::memoryReleaseInterval(*allocator_manager_));
  a.reset();
  // Release interval was configured to default value (1 second).
  step(std::chrono::milliseconds(1000));
  EXPECT_TRUE(TestUtility::waitForCounterEq(
      stats_, "memory_release_test.tcmalloc.released_by_timer", 1UL, time_system_));
  auto released_bytes_before_next_run = Stats::totalPageHeapUnmapped();
  b.reset();
  step(std::chrono::milliseconds(1000));
  EXPECT_TRUE(TestUtility::waitForCounterEq(
      stats_, "memory_release_test.tcmalloc.released_by_timer", 2UL, time_system_));
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LT(released_bytes_before_next_run, final_released_bytes);
  EXPECT_LT(initial_unmapped_bytes, final_released_bytes);
#endif
}

TEST_F(MemoryReleaseTest, ReleaseRateZeroNoRelease) {
  auto a = std::make_unique<unsigned char[]>(MB);
  EXPECT_LOG_NOT_CONTAINS(
      "info", "Configured tcmalloc with background release rate: 0 bytes 1000 milliseconds",
      initialiseAllocatorManager(0 /*bytes per second*/, 0));
  a.reset();
  // Release interval was configured to default value (1 second).
  step(std::chrono::milliseconds(3000));
  EXPECT_EQ(0UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
}

TEST_F(MemoryReleaseTest, ReleaseRateAboveZeroCustomIntervalMemoryReleased) {
  size_t initial_allocated_bytes = Stats::totalCurrentlyAllocated();
  auto a = std::make_unique<uint32_t[]>(40 * MB);
  auto b = std::make_unique<uint32_t[]>(40 * MB);
  if (Stats::totalCurrentlyAllocated() <= initial_allocated_bytes) {
    GTEST_SKIP() << "Skipping test, cannot measure memory usage precisely on this platform.";
  }
#if defined(GPERFTOOLS_TCMALLOC)
  EXPECT_LOG_CONTAINS("error",
                      "Memory releasing is not supported for gperf tcmalloc, no memory releasing "
                      "will be configured.",
                      initialiseAllocatorManager(MB /*bytes per second*/, 0));
#elif defined(TCMALLOC)
  auto initial_unmapped_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_CONTAINS(
      "info",
      "Configured tcmalloc with background release rate: 16777216 bytes per 2000 milliseconds",
      initialiseAllocatorManager(16 * MB /*bytes per second*/, 2));
  EXPECT_EQ(16 * MB, AllocatorManagerPeer::bytesToRelease(*allocator_manager_));
  EXPECT_EQ(std::chrono::milliseconds(2000),
            AllocatorManagerPeer::memoryReleaseInterval(*allocator_manager_));
  a.reset();
  step(std::chrono::milliseconds(2000));
  b.reset();
  step(std::chrono::milliseconds(2000));
  EXPECT_TRUE(TestUtility::waitForCounterEq(
      stats_, "memory_release_test.tcmalloc.released_by_timer", 2UL, time_system_));
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LT(initial_unmapped_bytes, final_released_bytes);
#endif
}

TEST_F(MemoryReleaseTest, ReleaseFreeMemory) {
#if !defined(TCMALLOC) && !defined(GPERFTOOLS_TCMALLOC)
  GTEST_SKIP() << "Test requires tcmalloc or gperftools.";
#endif
  initialiseAllocatorManager(100 * MB, 0);
  auto data = std::make_unique<unsigned char[]>(50 * MB);
  data.reset();
  auto unmapped_bytes_before_release = Stats::totalPageHeapUnmapped();
  allocator_manager_->releaseFreeMemory();
  auto unmapped_bytes_after_release = Stats::totalPageHeapUnmapped();
  EXPECT_LT(unmapped_bytes_before_release, unmapped_bytes_after_release);
}

TEST_F(MemoryReleaseTest, MaybeReleaseFreeMemory) {
#if !defined(TCMALLOC) && !defined(GPERFTOOLS_TCMALLOC)
  GTEST_SKIP() << "Test requires tcmalloc or gperftools.";
#endif
  size_t initial_allocated_bytes = Stats::totalCurrentlyAllocated();
  auto a = std::make_unique<uint32_t[]>(40 * MB);
  auto b = std::make_unique<uint32_t[]>(40 * MB);
  if (Stats::totalCurrentlyAllocated() <= initial_allocated_bytes) {
    GTEST_SKIP() << "Skipping test, cannot measure memory usage precisely on this platform.";
  }
  const uint64_t threshold = 100 * MB;
  initialiseAllocatorManager(200 * MB, 0, threshold);
  EXPECT_EQ(threshold,
            AllocatorManagerPeer::minimumUnfreedMemoryBytesThreshold(*allocator_manager_));

  // Release any remaining free memory to start fresh.
  allocator_manager_->releaseFreeMemory();

  // Allocate 150MB using 100KB chunks as allocating large chunk directly as it is returned back to
  // the system immediately on release.
  std::vector<std::unique_ptr<unsigned char[]>> chunks;
  const uint64_t chunk_size = 100 * 1024; // 100KB
  const int num_chunks = (150 * MB) / chunk_size;
  for (int i = 0; i < num_chunks; ++i) {
    chunks.push_back(std::make_unique<unsigned char[]>(chunk_size));
  }
  chunks.clear();
  auto unmapped_bytes_before_release = Stats::totalPageHeapUnmapped();
  allocator_manager_->maybeReleaseFreeMemory();
  auto unmapped_bytes_after_release = Stats::totalPageHeapUnmapped();
  EXPECT_LT(unmapped_bytes_before_release, unmapped_bytes_after_release);

  // Don't release memory if the threshold is not met.
  // Allocate 10MB using 100KB chunks
  const int num_chunks_small = (10 * MB) / chunk_size; // 102 chunks
  for (int i = 0; i < num_chunks_small; ++i) {
    chunks.push_back(std::make_unique<unsigned char[]>(chunk_size));
  }
  chunks.clear();
  unmapped_bytes_before_release = Stats::totalPageHeapUnmapped();
  allocator_manager_->maybeReleaseFreeMemory();
  unmapped_bytes_after_release = Stats::totalPageHeapUnmapped();
  EXPECT_EQ(unmapped_bytes_before_release, unmapped_bytes_after_release);
}

TEST_F(MemoryReleaseTest, CustomThresholdConfiguration) {
  initialiseAllocatorManager(MB, 0);
  EXPECT_EQ(100 * MB,
            AllocatorManagerPeer::minimumUnfreedMemoryBytesThreshold(*allocator_manager_));

  initialiseAllocatorManager(MB, 0, 200 * MB);
  EXPECT_EQ(200 * MB,
            AllocatorManagerPeer::minimumUnfreedMemoryBytesThreshold(*allocator_manager_));

  initialiseAllocatorManager(MB, 0, 10 * MB);
  EXPECT_EQ(10 * MB, AllocatorManagerPeer::minimumUnfreedMemoryBytesThreshold(*allocator_manager_));
}

} // namespace
} // namespace Memory
} // namespace Envoy
