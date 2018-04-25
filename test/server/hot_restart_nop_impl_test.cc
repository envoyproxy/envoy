#include "common/stats/stats_impl.h"

#include "server/hot_restart_nop_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Server {

class HotRestartNopImplTest : public testing::Test {
public:
  void setup() {
    Stats::RawStatData::configureForTestsOnly(options_);

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_nop_.reset(new HotRestartNopImpl(options_));
    hot_restart_nop_->drainParentListeners();
  }

  void TearDown() {
    // Configure it back so that later tests don't get the wonky values
    // used here
    NiceMock<MockOptions> default_options;
    Stats::RawStatData::configureForTestsOnly(default_options);
  }

  NiceMock<MockOptions> options_;
  std::vector<uint8_t> buffer_;
  std::unique_ptr<HotRestartNopImpl> hot_restart_nop_;
};

TEST_F(HotRestartNopImplTest, sameAlloc) {
  setup();

  Stats::RawStatData* stat1 = hot_restart_nop_->statsAllocator().alloc("stat1");
  Stats::RawStatData* stat2 = hot_restart_nop_->statsAllocator().alloc("stat2");
  Stats::RawStatData* stat3 = hot_restart_nop_->statsAllocator().alloc("stat3");
  Stats::RawStatData* stat4 = hot_restart_nop_->statsAllocator().alloc("stat4");
  Stats::RawStatData* stat5 = hot_restart_nop_->statsAllocator().alloc("stat5");
  hot_restart_nop_->statsAllocator().free(*stat2);
  hot_restart_nop_->statsAllocator().free(*stat4);
  stat2 = nullptr;
  stat4 = nullptr;

  Stats::RawStatData* stat1_prime = hot_restart_nop_->statsAllocator().alloc("stat1");
  Stats::RawStatData* stat3_prime = hot_restart_nop_->statsAllocator().alloc("stat3");
  Stats::RawStatData* stat5_prime = hot_restart_nop_->statsAllocator().alloc("stat5");
  EXPECT_EQ(stat1, stat1_prime);
  EXPECT_EQ(stat3, stat3_prime);
  EXPECT_EQ(stat5, stat5_prime);
}

} // namespace Server
} // namespace Envoy
