#include "source/common/event/dispatcher_impl.h"
#include "source/common/memory/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

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
};

namespace {

static const int MB = 1048576;

const std::string DefaultConfig = R"EOF(
  bytes_to_release: {}
)EOF";

const std::string ConfigWithInterval = R"EOF(
  bytes_to_release: {}
  memory_release_interval: {}s
)EOF";

class MemoryReleaseTest : public testing::Test {
protected:
  MemoryReleaseTest()
      : api_(Api::createApiForTest(stats_, time_system_)),
        dispatcher_("test_thread", *api_, time_system_), scope_("memory_release_test.", stats_) {}

  void initialiseAllocatorManager(uint64_t bytes_to_release, float release_interval_s) {
    std::string yaml_config;
    if (release_interval_s > 0) {
      yaml_config = fmt::format(ConfigWithInterval, bytes_to_release, release_interval_s);
    } else {
      yaml_config = fmt::format(DefaultConfig, bytes_to_release);
    }
    const auto proto_config =
        TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
    allocator_manager_ = std::make_unique<Memory::AllocatorManager>(
        dispatcher_, Thread::threadFactoryForTest(), scope_, proto_config);
  }

  void step(const std::chrono::milliseconds& step) {
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(step), dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
  }

  Envoy::Stats::TestUtil::TestStore stats_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherImpl dispatcher_;
  Envoy::Stats::TestUtil::TestScope scope_;
  std::unique_ptr<Memory::AllocatorManager> allocator_manager_;
};

TEST_F(MemoryReleaseTest, RelaseRateAboveZeroDefaultIntervalMemoryReleased) {
  size_t initial_allocated_bytes = Stats::totalCurrentlyAllocated();
  auto a = std::make_unique<unsigned char[]>(MB);
  auto b = std::make_unique<unsigned char[]>(MB);
  if (Stats::totalCurrentlyAllocated() - initial_allocated_bytes <= 0) {
    // Cannot measure memory usage precisely on this platform.
    return;
  }
  auto initial_unmapped_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_CONTAINS(
      "info", "Configured tcmalloc with background release rate: 512 bytes per 1000 milliseconds",
      initialiseAllocatorManager(512 /*bytes per second*/, 0));
  EXPECT_EQ(512, AllocatorManagerPeer::bytesToRelease(*allocator_manager_));
  EXPECT_EQ(std::chrono::milliseconds(1000),
            AllocatorManagerPeer::memoryReleaseInterval(*allocator_manager_));
  a.reset();
  // Release interval was configured to default value (1 second).
  step(std::chrono::milliseconds(1000));
  EXPECT_EQ(1UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  auto released_bytes_before_next_run = Stats::totalPageHeapUnmapped();
  b.reset();
  step(std::chrono::milliseconds(2000));
  EXPECT_EQ(2UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  Stats::dumpStatsToLog();
#if defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC)
  EXPECT_LT(released_bytes_before_next_run, final_released_bytes);
  EXPECT_LT(initial_unmapped_bytes, final_released_bytes);
#else
  EXPECT_LE(released_bytes_before_next_run, final_released_bytes);
  EXPECT_LE(initial_unmapped_bytes, final_released_bytes);
#endif
}

TEST_F(MemoryReleaseTest, RelaseRateZeroNoRelease) {
  auto a = std::make_unique<unsigned char[]>(MB);
  uint64_t before = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_NOT_CONTAINS(
      "info", "Configured tcmalloc with background release rate: 0 bytes 1000 milliseconds",
      initialiseAllocatorManager(0 /*bytes per second*/, 0));
  a.reset();
  // Release interval was configured to default value (1 second).
  step(std::chrono::milliseconds(3000));
  EXPECT_EQ(0UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  uint64_t after = Stats::totalPageHeapUnmapped();
  EXPECT_EQ(after, before);
}

TEST_F(MemoryReleaseTest, RelaseRateAboveZeroCustomIntervalMemoryReleased) {
  size_t initial_allocated_bytes = Stats::totalCurrentlyAllocated();
  auto a = std::make_unique<uint32_t[]>(512 * MB);
  auto b = std::make_unique<uint32_t[]>(512 * MB);
  if (Stats::totalCurrentlyAllocated() - initial_allocated_bytes <= 0) {
    // Cannot measure memory usage precisely on this platform...
    return;
  }
  auto initial_unmapped_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_CONTAINS(
      "info",
      "Configured tcmalloc with background release rate: 67108864 bytes per 2000 milliseconds",
      initialiseAllocatorManager(64 * MB /*bytes per second*/, 2));
  EXPECT_EQ(64 * MB, AllocatorManagerPeer::bytesToRelease(*allocator_manager_));
  EXPECT_EQ(std::chrono::milliseconds(2000),
            AllocatorManagerPeer::memoryReleaseInterval(*allocator_manager_));
  a.reset();
  step(std::chrono::milliseconds(2000));
  b.reset();
  step(std::chrono::milliseconds(2000));
  EXPECT_EQ(2UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  Stats::dumpStatsToLog();
#if defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC)
  EXPECT_LT(initial_unmapped_bytes, final_released_bytes);
#else
  EXPECT_LE(initial_unmapped_bytes, final_released_bytes);
#endif
}

} // namespace
} // namespace Memory
} // namespace Envoy
