#include "common/api/os_sys_calls_impl.h"
#include "common/common/hex.h"
#include "common/stats/stats_impl.h"

#include "server/hot_restart_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;
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
    EXPECT_CALL(options_, statsOptions()).WillRepeatedly(ReturnRef(stats_options_));

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_.reset(new HotRestartImpl(options_));
    hot_restart_->drainParentListeners();
  }

  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  NiceMock<MockOptions> options_;
  Stats::StatsOptionsImpl stats_options_;
  std::vector<uint8_t> buffer_;
  std::unique_ptr<HotRestartImpl> hot_restart_;
};

TEST_F(HotRestartImplTest, versionString) {
  // Tests that the version-string will be consistent and SharedMemory::VERSION,
  // between multiple instantiations.
  std::string version;
  uint64_t max_stats, max_obj_name_length;

  // The mocking infrastructure requires a test setup and teardown every time we
  // want to re-instantiate HotRestartImpl.
  {
    setup();
    version = hot_restart_->version();
    EXPECT_TRUE(absl::StartsWith(version, fmt::format("{}.", SharedMemory::VERSION))) << version;
    max_stats = options_.maxStats(); // Save this so we can double it below.
    max_obj_name_length = options_.statsOptions().maxObjNameLength();
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
    EXPECT_NE(version, hot_restart_->version()) << "Version changes when max-stats change";
    TearDown();
  }

  {
    stats_options_.max_obj_name_length_ = 2 * max_obj_name_length;
    setup();
    EXPECT_NE(version, hot_restart_->version())
        << "Version changes when max-obj-name-length changes";
    // TearDown is called automatically at end of test.
  }
}

// Check consistency of internal raw stat representation by comparing hash of
// memory contents against a previously recorded value.
TEST_F(HotRestartImplTest, Consistency) {
  setup();

  // Generate a stat, encode it to hex, and take the hash of that hex string. We
  // expect the hash to vary only when the internal representation of a stat has
  // been intentionally changed, in which case SharedMemory::VERSION should be
  // incremented as well.
  const uint64_t expected_hash = 1874506077228772558;
  const uint64_t max_name_length = stats_options_.maxNameLength();

  const std::string name_1(max_name_length, 'A');
  Stats::RawStatData* stat_1 = hot_restart_->alloc(name_1);
  const uint64_t stat_size = sizeof(Stats::RawStatData) + max_name_length;
  const std::string stat_hex_dump_1 = Hex::encode(reinterpret_cast<uint8_t*>(stat_1), stat_size);
  EXPECT_EQ(HashUtil::xxHash64(stat_hex_dump_1), expected_hash);
  EXPECT_EQ(name_1, stat_1->key());
  hot_restart_->free(*stat_1);
}

TEST_F(HotRestartImplTest, RawAlloc) {
  setup();

  Stats::RawStatData* stat_1 = hot_restart_->alloc("ref_name");
  ASSERT_NE(stat_1, nullptr);
  Stats::RawStatData* stat_2 = hot_restart_->alloc("ref_name");
  ASSERT_NE(stat_2, nullptr);
  Stats::RawStatData* stat_3 = hot_restart_->alloc("not_ref_name");
  ASSERT_NE(stat_3, nullptr);
  EXPECT_EQ(stat_1, stat_2);
  EXPECT_NE(stat_1, stat_3);
  EXPECT_NE(stat_2, stat_3);
  hot_restart_->free(*stat_1);
  hot_restart_->free(*stat_2);
  hot_restart_->free(*stat_3);
}

TEST_F(HotRestartImplTest, crossAlloc) {
  setup();

  Stats::RawStatData* stat1 = hot_restart_->alloc("stat1");
  Stats::RawStatData* stat2 = hot_restart_->alloc("stat2");
  Stats::RawStatData* stat3 = hot_restart_->alloc("stat3");
  Stats::RawStatData* stat4 = hot_restart_->alloc("stat4");
  Stats::RawStatData* stat5 = hot_restart_->alloc("stat5");
  hot_restart_->free(*stat2);
  hot_restart_->free(*stat4);
  stat2 = nullptr;
  stat4 = nullptr;

  EXPECT_CALL(options_, restartEpoch()).WillRepeatedly(Return(1));
  EXPECT_CALL(os_sys_calls_, shmOpen(_, _, _));
  EXPECT_CALL(os_sys_calls_, mmap(_, _, _, _, _, _)).WillOnce(Return(buffer_.data()));
  EXPECT_CALL(os_sys_calls_, bind(_, _, _));
  HotRestartImpl hot_restart2(options_);
  Stats::RawStatData* stat1_prime = hot_restart2.alloc("stat1");
  Stats::RawStatData* stat3_prime = hot_restart2.alloc("stat3");
  Stats::RawStatData* stat5_prime = hot_restart2.alloc("stat5");
  EXPECT_EQ(stat1, stat1_prime);
  EXPECT_EQ(stat3, stat3_prime);
  EXPECT_EQ(stat5, stat5_prime);
}

TEST_F(HotRestartImplTest, allocFail) {
  EXPECT_CALL(options_, maxStats()).WillRepeatedly(Return(2));
  setup();

  Stats::RawStatData* s1 = hot_restart_->alloc("1");
  Stats::RawStatData* s2 = hot_restart_->alloc("2");
  Stats::RawStatData* s3 = hot_restart_->alloc("3");
  EXPECT_NE(s1, nullptr);
  EXPECT_NE(s2, nullptr);
  EXPECT_EQ(s3, nullptr);
}

// Because the shared memory is managed manually, make sure it meets
// basic requirements:
//   - Objects are correctly aligned so that std::atomic works properly
//   - Objects don't overlap
class HotRestartImplAlignmentTest : public HotRestartImplTest,
                                    public testing::WithParamInterface<uint64_t> {
public:
  HotRestartImplAlignmentTest() : name_len_(8 + GetParam()) {
    stats_options_.max_obj_name_length_ = name_len_;
    EXPECT_CALL(options_, statsOptions()).WillRepeatedly(ReturnRef(stats_options_));
    EXPECT_CALL(options_, maxStats()).WillRepeatedly(Return(num_stats_));

    setup();
    EXPECT_EQ(name_len_ + stats_options_.maxStatSuffixLength(), stats_options_.maxNameLength());
  }

  Stats::StatsOptionsImpl stats_options_;
  static const uint64_t num_stats_ = 8;
  const uint64_t name_len_;
};

TEST_P(HotRestartImplAlignmentTest, objectAlignment) {

  std::set<Stats::RawStatData*> used;
  for (uint64_t i = 0; i < num_stats_; i++) {
    Stats::RawStatData* stat = hot_restart_->alloc(fmt::format("stat {}", i));
    EXPECT_TRUE((reinterpret_cast<uintptr_t>(stat) % alignof(decltype(*stat))) == 0);
    EXPECT_TRUE(used.find(stat) == used.end());
    used.insert(stat);
  }
}

TEST_P(HotRestartImplAlignmentTest, objectOverlap) {
  // Iterate through all stats forwards and backwards, writing to all fields, then read them back to
  // make sure that writing to an adjacent stat didn't overwrite
  struct TestStat {
    Stats::RawStatData* stat_;
    std::string name_;
    uint64_t index_;
  };
  std::vector<TestStat> stats;
  for (uint64_t i = 0; i < num_stats_; i++) {
    std::string name = fmt::format("{}zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"
                                   "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"
                                   "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
                                   i)
                           .substr(0, stats_options_.maxNameLength());
    TestStat ts;
    ts.stat_ = hot_restart_->alloc(name);
    ts.name_ = ts.stat_->name_;
    ts.index_ = i;

    // If this isn't true then the hard coded part of the name isn't long enough to make the test
    // valid.
    EXPECT_EQ(ts.name_.size(), stats_options_.maxNameLength());

    stats.push_back(ts);
  }

  auto write = [](TestStat& ts) {
    ts.stat_->value_ = ts.index_;
    ts.stat_->pending_increment_ = ts.index_;
    ts.stat_->flags_ = ts.index_;
    ts.stat_->ref_count_ = ts.index_;
    ts.stat_->unused_ = ts.index_;
  };

  auto verify = [](TestStat& ts) {
    EXPECT_EQ(ts.stat_->key(), ts.name_);
    EXPECT_EQ(ts.stat_->value_, ts.index_);
    EXPECT_EQ(ts.stat_->pending_increment_, ts.index_);
    EXPECT_EQ(ts.stat_->flags_, ts.index_);
    EXPECT_EQ(ts.stat_->ref_count_, ts.index_);
    EXPECT_EQ(ts.stat_->unused_, ts.index_);
  };

  for (TestStat& ts : stats) {
    write(ts);
  }

  for (TestStat& ts : stats) {
    verify(ts);
  }

  for (auto it = stats.rbegin(); it != stats.rend(); ++it) {
    write(*it);
  }

  for (auto it = stats.rbegin(); it != stats.rend(); ++it) {
    verify(*it);
  }
}

INSTANTIATE_TEST_CASE_P(HotRestartImplAlignmentTest, HotRestartImplAlignmentTest,
                        testing::Range(0UL, alignof(Stats::RawStatData) + 1));

} // namespace Server
} // namespace Envoy
