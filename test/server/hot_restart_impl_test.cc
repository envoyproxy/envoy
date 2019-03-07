#include <memory>

#include "common/api/os_sys_calls_impl.h"
#include "common/common/hex.h"

#include "server/hot_restart_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;
using testing::WithArg;

namespace Envoy {
namespace Server {
namespace {

class HotRestartImplTest : public testing::Test {
public:
  void setup() {
    EXPECT_CALL(os_sys_calls_, shmUnlink(_)).Times(AnyNumber());
    EXPECT_CALL(os_sys_calls_, shmOpen(_, _, _));
    EXPECT_CALL(os_sys_calls_, ftruncate(_, _)).WillOnce(WithArg<1>(Invoke([this](off_t size) {
      buffer_.resize(size);
      return Api::SysCallIntResult{0, 0};
    })));
    EXPECT_CALL(os_sys_calls_, mmap(_, _, _, _, _, _)).WillOnce(InvokeWithoutArgs([this]() {
      return Api::SysCallPtrResult{buffer_.data(), 0};
    }));
    // We bind two sockets: one to talk to parent, one to talk to our (hypothetical eventual) child
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(2);
    EXPECT_CALL(options_, statsOptions()).WillRepeatedly(ReturnRef(stats_options_));

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_ = std::make_unique<HotRestartImpl>(options_);
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
  // Tests that the version-string will be consistent and HOT_RESTART_VERSION,
  // between multiple instantiations.
  std::string version;
  uint64_t max_stats, max_obj_name_length;

  // The mocking infrastructure requires a test setup and teardown every time we
  // want to re-instantiate HotRestartImpl.
  {
    setup();
    version = hot_restart_->version();
    EXPECT_TRUE(absl::StartsWith(version, fmt::format("{}.", HOT_RESTART_VERSION))) << version;
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
  // been intentionally changed, in which case HOT_RESTART_VERSION should be
  // incremented as well.
  const uint64_t expected_hash = 428480280747063249;
  const uint64_t max_name_length = stats_options_.maxNameLength();

  const std::string name_1(max_name_length, 'A');
  Stats::HeapStatData* stat_1 = hot_restart_->statsAllocator().alloc(name_1);
  const uint64_t stat_size = sizeof(Stats::HeapStatData) + max_name_length;
  const std::string stat_hex_dump_1 = Hex::encode(reinterpret_cast<uint8_t*>(stat_1), stat_size);
  EXPECT_EQ(HashUtil::xxHash64(stat_hex_dump_1), expected_hash);
  EXPECT_EQ(name_1, stat_1->key());
  hot_restart_->statsAllocator().free(*stat_1);
}

} // namespace
} // namespace Server
} // namespace Envoy
