#include "source/common/event/dispatcher_impl.h"
#include "source/common/memory/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

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

const std::string DefaultConfig = R"EOF(
  bytes_to_release: {}
)EOF";

class MemoryReleaseTest : public testing::Test {
protected:
  MemoryReleaseTest()
      : api_(Api::createApiForTest(stats_, time_system_)),
        dispatcher_("test_thread", *api_, time_system_), scope_("memory_release_test.", stats_) {}

  void initialiseAllocatorManager(uint64_t bytes_to_release, const std::chrono::milliseconds&) {
    const auto yaml_config = fmt::format(DefaultConfig, bytes_to_release);
    const auto proto_config =
        TestUtility::parseYaml<envoy::config::bootstrap::v3::MemoryAllocatorManager>(yaml_config);
    memory_allocator_ = std::make_unique<Memory::AllocatorManager>(
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
  std::unique_ptr<Memory::AllocatorManager> memory_allocator_;
};

TEST_F(MemoryReleaseTest, RelaseRateAboveZeroMemoryReleased) {
  static const int MB = 1048576;
  size_t initial_allocated_bytes = Stats::totalCurrentlyAllocated();
  auto a = std::make_unique<unsigned char[]>(MB);
  auto b = std::make_unique<unsigned char[]>(MB);
  if (Stats::totalCurrentlyAllocated() - initial_allocated_bytes <= 0) {
    // Cannot measure memory usage precisely on this platform.
    return;
  }
  auto initial_unmapped_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_CONTAINS(
      "info",
      "Configured tcmalloc with background release rate: 1048576 bytes per 1000 milliseconds",
      initialiseAllocatorManager(MB /*bytes per second*/, std::chrono::milliseconds(1000)));
  a.reset();
  // Release interval was configured to default value (1 second).
  step(std::chrono::milliseconds(1000));
  EXPECT_EQ(1UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  auto released_bytes_before_next_run = Stats::totalPageHeapUnmapped();
  b.reset();
  step(std::chrono::milliseconds(2000));
  EXPECT_EQ(2UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  auto final_released_bytes = Stats::totalPageHeapUnmapped();
  EXPECT_LE(released_bytes_before_next_run, final_released_bytes);
  EXPECT_LE(initial_unmapped_bytes, final_released_bytes);
}

TEST_F(MemoryReleaseTest, RelaseRateZeroNoRelease) {
  auto ptr = std::make_unique<MyStruct>();
  uint64_t before = Stats::totalPageHeapUnmapped();
  EXPECT_LOG_NOT_CONTAINS(
      "info", "Configured tcmalloc with background release rate: 0 bytes 1000 milliseconds",
      initialiseAllocatorManager(0 /*bytes per second*/, std::chrono::milliseconds(1000)));
  // Release interval was configured to default value (1 second).
  step(std::chrono::milliseconds(3000));
  EXPECT_EQ(0UL, stats_.counter("memory_release_test.tcmalloc.released_by_timer").value());
  uint64_t after = Stats::totalPageHeapUnmapped();
  EXPECT_GE(after, before);
}

#endif // ENVOY_MEMORY_DEBUG_ENABLED

} // namespace
} // namespace Memory
} // namespace Envoy
