#include "common/api/os_sys_calls_impl.h"
#include "common/stats/stats_impl.h"

#include "server/hot_restart_impl.h"

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

class HotRestartImplTest : public testing::Test {
public:
  void setup() {
    EXPECT_CALL(os_sys_calls_, shmUnlink(_));
    EXPECT_CALL(os_sys_calls_, shmOpen(_, _, _));
    EXPECT_CALL(os_sys_calls_, ftruncate(_, _)).WillOnce(WithArg<1>(Invoke([this](off_t size) {
      buffer_.resize(size);
      return 0;
    })));
    EXPECT_CALL(os_sys_calls_, mmap(_, _, _, _, _, _)).WillOnce(InvokeWithoutArgs([this]() {
      return buffer_.data();
    }));
    EXPECT_CALL(os_sys_calls_, bind(_, _, _));

    Stats::RawStatData::configureForTestsOnly(options_);

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_.reset(new HotRestartImpl(options_));
    hot_restart_->drainParentListeners();
  }

  void TearDown() {
    // Configure it back so that later tests don't get the wonky values
    // used here
    NiceMock<MockOptions> default_options;
    Stats::RawStatData::configureForTestsOnly(default_options);
  }

  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  NiceMock<MockOptions> options_;
  std::vector<uint8_t> buffer_;
  std::unique_ptr<HotRestartImpl> hot_restart_;
};

TEST_F(HotRestartImplTest, versionString) {
  // Tests that the version-string will be consistent and SharedMemory::VERSION,
  // between multiple instantiations.
  std::string version;
  uint64_t max_stats;

  // The mocking infrastructure requires a test setup and teardown every time we
  // want to re-instantiate HotRestartImpl.
  {
    setup();
    version = hot_restart_->version();
    EXPECT_TRUE(absl::StartsWith(version, fmt::format("{}.", SharedMemory::VERSION))) << version;
    max_stats = options_.maxStats(); // Save this so we can double it below.
    TearDown();
  }

  {
    setup();
    EXPECT_EQ(version, hot_restart_->version()) << "Version string deterministic from options";
    TearDown();
  }

  {
    ON_CALL(options_, maxStats()).WillByDefault(Return(2 * max_stats));
    setup();
    EXPECT_NE(version, hot_restart_->version()) << "Version changes when options change";
    // TearDown is called automatically at end of test.
  }
}

TEST_F(HotRestartImplTest, crossAlloc) {
  setup();

  Stats::RawStatData* stat1 = hot_restart_->statsAllocator().alloc("stat1");
  Stats::RawStatData* stat2 = hot_restart_->statsAllocator().alloc("stat2");
  Stats::RawStatData* stat3 = hot_restart_->statsAllocator().alloc("stat3");
  Stats::RawStatData* stat4 = hot_restart_->statsAllocator().alloc("stat4");
  Stats::RawStatData* stat5 = hot_restart_->statsAllocator().alloc("stat5");
  hot_restart_->statsAllocator().free(*stat2);
  hot_restart_->statsAllocator().free(*stat4);
  stat2 = nullptr;
  stat4 = nullptr;

  EXPECT_CALL(options_, restartEpoch()).WillRepeatedly(Return(1));
  EXPECT_CALL(os_sys_calls_, shmOpen(_, _, _));
  EXPECT_CALL(os_sys_calls_, mmap(_, _, _, _, _, _)).WillOnce(Return(buffer_.data()));
  EXPECT_CALL(os_sys_calls_, bind(_, _, _));
  HotRestartImpl hot_restart2(options_);
  Stats::RawStatData* stat1_prime = hot_restart2.statsAllocator().alloc("stat1");
  Stats::RawStatData* stat3_prime = hot_restart2.statsAllocator().alloc("stat3");
  Stats::RawStatData* stat5_prime = hot_restart2.statsAllocator().alloc("stat5");
  EXPECT_EQ(stat1, stat1_prime);
  EXPECT_EQ(stat3, stat3_prime);
  EXPECT_EQ(stat5, stat5_prime);
}

} // namespace Server
} // namespace Envoy
