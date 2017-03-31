#include "common/common/utility.h"

#include "server/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

using testing::InSequence;
using testing::NiceMock;

namespace Server {

/**
 * Death test caveat: Because of the way we die gcov doesn't receive coverage
 * information from the forked process that is checked for succesful death.
 * This means that the lines dealing with the calls to PANIC are not seen as
 * green in the coverage report. However, rest assured from the results of the
 * test: these lines are in fact covered.
 */
class GuardDogDeathTest : public testing::Test {
protected:
  GuardDogDeathTest()
      : config_kill_(1000, 1000, 100, 1000), config_multikill_(1000, 1000, 1000, 500),
        time_point_(std::chrono::system_clock::now()) {}

  NiceMock<Configuration::MockMain> config_kill_;
  NiceMock<Configuration::MockMain> config_multikill_;
  NiceMock<Stats::MockStore> fakestats_;
  MockSystemTimeSource time_source_;
  std::chrono::system_clock::time_point time_point_;
};

class GuardDogMissTest : public testing::Test {
protected:
  GuardDogMissTest() : config_miss(1, 1000, 0, 0), config_mega(1000, 1, 0, 0) {}

  NiceMock<Configuration::MockMain> config_miss;
  NiceMock<Configuration::MockMain> config_mega;
  NiceMock<Stats::MockStore> stats_store;
  MockSystemTimeSource time_source_;
  std::chrono::system_clock::time_point time_point_;
};

TEST_F(GuardDogDeathTest, KillDeathTest) {
  // Is it German for "The Function"? Almost...
  auto die_function = [&]() -> void {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    GuardDogImpl gd(fakestats_, config_kill_, time_source_);
    auto unpet_dog = gd.createWatchDog(0);
    gd.forceCheckForTest();
    time_point_ += std::chrono::milliseconds(500);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    gd.forceCheckForTest();
  };
  // Why do it this way? Any threads must be started inside the death test
  // statement and this is the easiest way to accomplish that.
  EXPECT_DEATH(die_function(), "");
}

TEST_F(GuardDogDeathTest, MultiKillDeathTest) {
  auto die_function = [&]() -> void {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    GuardDogImpl gd(fakestats_, config_multikill_, time_source_);
    auto unpet_dog1 = gd.createWatchDog(0);
    gd.forceCheckForTest();
    auto unpet_dog2 = gd.createWatchDog(1);
    gd.forceCheckForTest();
    time_point_ += std::chrono::milliseconds(501);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    gd.forceCheckForTest();
  };
  EXPECT_DEATH(die_function(), "");
}

TEST_F(GuardDogDeathTest, NearDeathTest) {
  InSequence s;
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  GuardDogImpl gd(fakestats_, config_multikill_, time_source_);
  auto unpet_dog = gd.createWatchDog(0);
  auto pet_dog = gd.createWatchDog(1);
  for (int i = 0; i < 6; i++) {
    time_point_ += std::chrono::milliseconds(100);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    pet_dog->touch();
    gd.forceCheckForTest();
  }
}

TEST_F(GuardDogMissTest, MissTest) {
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  GuardDogImpl gd(stats_store, config_miss, time_source_);
  auto unpet_dog = gd.createWatchDog(0);
  time_point_ += std::chrono::milliseconds(501);
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  gd.forceCheckForTest();
  gd.stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST_F(GuardDogMissTest, MegaMissTest) {
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  GuardDogImpl gd(stats_store, config_mega, time_source_);
  auto unpet_dog = gd.createWatchDog(0);
  time_point_ += std::chrono::milliseconds(501);
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  gd.forceCheckForTest();
  gd.stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST(GuardDogBasicTest, StartStopTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(0, 0, 0, 0);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
}

TEST(GuardDogBasicTest, LoopIntervalNoKillTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(40, 50, 0, 0);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  EXPECT_EQ(gd.loopIntervalForTest(), 40);
}

TEST(GuardDogBasicTest, LoopIntervalTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(100, 90, 1000, 500);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  EXPECT_EQ(gd.loopIntervalForTest(), 90);
}

TEST(WatchDogBasicTest, ThreadIdTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(100, 90, 1000, 500);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  auto watched_dog = gd.createWatchDog(123);
  EXPECT_EQ(watched_dog->threadId(), 123);
  gd.stopWatching(watched_dog);
}
// If this test fails it is because the SystemTime object has become nontrivial
// or we are compiling under a compiler and library combo that makes the
// SystemTime object require a lock to be atomicly modified.
//
// The WatchDog/GuardDog relies on this being a lock free atomic for perf
// reasons so some workaround will be required if this test starts failing.
TEST(WatchDogTimeTest, AtomicIsAtomicTest) {
  ASSERT(std::atomic<ProdSystemTimeSource>{}.is_lock_free());
}

} // Server
