#include "source/common/memory/stats.h"

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
  static size_t backgroundReleaseRateBytesPerSecond(const AllocatorManager& allocator_manager) {
    return allocator_manager.background_release_rate_bytes_per_second_;
  }
};

namespace {

static const int MB = 1048576;

class MemoryReleaseTest : public testing::Test {
protected:
  MemoryReleaseTest() : api_(Api::createApiForTest()) {}

  void initialiseAllocatorManager(uint64_t bytes_to_release, float release_interval_s) {
    const std::string yaml_config = (release_interval_s > 0)
                                        ? fmt::format(R"EOF(
  bytes_to_release: {}
  memory_release_interval: {}s
)EOF",
                                                      bytes_to_release, release_interval_s)
                                        : fmt::format(R"EOF(
  bytes_to_release: {}
)EOF",
                                                      bytes_to_release);
    const auto proto_config =
        TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
    allocator_manager_ = std::make_unique<Memory::AllocatorManager>(*api_, proto_config);
  }

  Api::ApiPtr api_;
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
  EXPECT_LOG_CONTAINS("info",
                      "Configured tcmalloc with background release rate: 1048576 bytes per second.",
                      initialiseAllocatorManager(MB /*bytes per second*/, 0));
  EXPECT_EQ(MB, AllocatorManagerPeer::bytesToRelease(*allocator_manager_));
  EXPECT_EQ(std::chrono::milliseconds(1000),
            AllocatorManagerPeer::memoryReleaseInterval(*allocator_manager_));
  EXPECT_EQ(static_cast<size_t>(MB),
            AllocatorManagerPeer::backgroundReleaseRateBytesPerSecond(*allocator_manager_));
  a.reset();
  b.reset();
  // Wait for ProcessBackgroundActions to release memory. The default sleep interval is 1 second.
  absl::SleepFor(absl::Seconds(3));
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LT(initial_unmapped_bytes, final_released_bytes);
#endif
}

TEST_F(MemoryReleaseTest, ReleaseRateZeroNoBackgroundThread) {
  EXPECT_LOG_NOT_CONTAINS("info",
                          "Configured tcmalloc with background release rate: 0 bytes per second.",
                          initialiseAllocatorManager(0 /*bytes per second*/, 0));
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
  // 16 MB every 2 seconds = 8 MB/s.
  EXPECT_LOG_CONTAINS("info",
                      "Configured tcmalloc with background release rate: 8388608 bytes per second.",
                      initialiseAllocatorManager(16 * MB /*bytes per 2 seconds*/, 2));
  EXPECT_EQ(16 * MB, AllocatorManagerPeer::bytesToRelease(*allocator_manager_));
  EXPECT_EQ(std::chrono::milliseconds(2000),
            AllocatorManagerPeer::memoryReleaseInterval(*allocator_manager_));
  // Verify the computed release rate: 16 MB * 1000 / 2000 = 8 MB/s.
  EXPECT_EQ(static_cast<size_t>(8 * MB),
            AllocatorManagerPeer::backgroundReleaseRateBytesPerSecond(*allocator_manager_));
  a.reset();
  b.reset();
  // Wait for ProcessBackgroundActions to release memory.
  absl::SleepFor(absl::Seconds(3));
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LT(initial_unmapped_bytes, final_released_bytes);
#endif
}

TEST_F(MemoryReleaseTest, BackgroundReleaseRateComputedCorrectly) {
#if defined(TCMALLOC)
  // 4 MB every 500ms = 8 MB/s.
  initialiseAllocatorManager(4 * MB, 0.5);
  EXPECT_EQ(static_cast<size_t>(8 * MB),
            AllocatorManagerPeer::backgroundReleaseRateBytesPerSecond(*allocator_manager_));
  allocator_manager_.reset();

  // 1 MB every 1s (default) = 1 MB/s.
  initialiseAllocatorManager(MB, 0);
  EXPECT_EQ(static_cast<size_t>(MB),
            AllocatorManagerPeer::backgroundReleaseRateBytesPerSecond(*allocator_manager_));
  allocator_manager_.reset();

  // 10 MB every 5s = 2 MB/s.
  initialiseAllocatorManager(10 * MB, 5);
  EXPECT_EQ(static_cast<size_t>(2 * MB),
            AllocatorManagerPeer::backgroundReleaseRateBytesPerSecond(*allocator_manager_));
#endif
}

TEST_F(MemoryReleaseTest, MaxUnfreedMemoryBytesConfigured) {
  EXPECT_EQ(DEFAULT_MAX_UNFREED_MEMORY_BYTES, maxUnfreedMemoryBytes());
  const std::string yaml_config = R"EOF(
  max_unfreed_memory_bytes: 52428800
)EOF";
  const auto proto_config =
      TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
  EXPECT_LOG_CONTAINS("info", "Set max unfreed memory threshold to 52428800 bytes.",
                      allocator_manager_ =
                          std::make_unique<Memory::AllocatorManager>(*api_, proto_config));
  EXPECT_EQ(52428800, maxUnfreedMemoryBytes());
  // Reset to default for other tests.
  setMaxUnfreedMemoryBytes(DEFAULT_MAX_UNFREED_MEMORY_BYTES);
}

TEST_F(MemoryReleaseTest, MaxUnfreedMemoryBytesDefaultWhenZero) {
  setMaxUnfreedMemoryBytes(DEFAULT_MAX_UNFREED_MEMORY_BYTES);
  const std::string yaml_config = R"EOF(
  max_unfreed_memory_bytes: 0
)EOF";
  const auto proto_config =
      TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
  EXPECT_LOG_NOT_CONTAINS("info", "Set max unfreed memory threshold",
                          allocator_manager_ =
                              std::make_unique<Memory::AllocatorManager>(*api_, proto_config));
  EXPECT_EQ(DEFAULT_MAX_UNFREED_MEMORY_BYTES, maxUnfreedMemoryBytes());
}

TEST_F(MemoryReleaseTest, SoftMemoryLimitConfigured) {
  const std::string yaml_config = R"EOF(
  soft_memory_limit_bytes: 1073741824
)EOF";
  const auto proto_config =
      TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
#if defined(TCMALLOC)
  EXPECT_LOG_CONTAINS("info", "Set tcmalloc soft memory limit to 1073741824 bytes.",
                      allocator_manager_ =
                          std::make_unique<Memory::AllocatorManager>(*api_, proto_config));
#else
  EXPECT_LOG_CONTAINS(
      "warn", "Soft memory limit is only supported with Google's tcmalloc, ignoring.",
      allocator_manager_ = std::make_unique<Memory::AllocatorManager>(*api_, proto_config));
#endif
}

TEST_F(MemoryReleaseTest, MaxPerCpuCacheSizeConfigured) {
  const std::string yaml_config = R"EOF(
  max_per_cpu_cache_size_bytes: 2097152
)EOF";
  const auto proto_config =
      TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
#if defined(TCMALLOC)
  EXPECT_LOG_CONTAINS("info", "Set tcmalloc max per-CPU cache size to 2097152 bytes.",
                      allocator_manager_ =
                          std::make_unique<Memory::AllocatorManager>(*api_, proto_config));
#else
  EXPECT_LOG_CONTAINS(
      "warn", "Max per-CPU cache size is only supported with Google's tcmalloc, ignoring.",
      allocator_manager_ = std::make_unique<Memory::AllocatorManager>(*api_, proto_config));
#endif
}

} // namespace
} // namespace Memory
} // namespace Envoy
