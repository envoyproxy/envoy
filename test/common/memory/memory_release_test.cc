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

//#ifdef ENVOY_MEMORY_DEBUG_ENABLED

constexpr int ArraySize = 10;

struct MyStruct {
  MyStruct() : x_(0) {} // words_ is uninitialized; will have whatever allocator left there.
  uint64_t x_;
  uint64_t words_[ArraySize];
};

class MemoryReleaseTest : public testing::Test {
protected:
  MemoryReleaseTest()
      : api_(Api::createApiForTest(stats_, time_system_))
      //,
        //dispatcher_("test_thread", *api_, time_system_)
        {}

  void initialiseAllocator(const uint64_t background_release_rate) {
    memory_allocator_ = std::make_unique<Memory::Allocator>(api_->threadFactory(), background_release_rate);
  }

//   void step() {
//     time_system_.advanceTimeAndRun(std::chrono::milliseconds(10000), dispatcher_,
//                                    Event::Dispatcher::RunType::NonBlock);
//   }

  Envoy::Stats::TestUtil::TestStore stats_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  std::unique_ptr<Memory::Allocator> memory_allocator_;
  //Event::DispatcherImpl dispatcher_;
};

// TEST_F(MemoryReleaseTest, RelaseRateZeroNoRelease) {
//   uint64_t before = Stats::totalCurrentlyAllocated();
//   auto ptr = std::make_unique<MyStruct>();
//   uint64_t after = Stats::totalCurrentlyAllocated();
//   EXPECT_LE(sizeof(MyStruct), after - before);
// }

TEST_F(MemoryReleaseTest, RelaseRateAboveZeroMemoryReleased) {
  auto ptr = std::make_unique<MyStruct>();
  uint64_t before = Stats::totalCurrentlyAllocated();
  initialiseAllocator(512 /*bytes per second*/);
  uint64_t after = Stats::totalCurrentlyAllocated();
  EXPECT_LE(sizeof(MyStruct), after - before);
}

//#endif // ENVOY_MEMORY_DEBUG_ENABLED

} // namespace
} // namespace Memory
} // namespace Envoy
