#include "common/common/utility.h"
#include "common/event/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

using testing::InSequence;

namespace Event {

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

  void startNeglectedDogAndWait() {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    GuardDogImpl gd(fakestats_, config_kill_, time_source_);
    auto unpet_dog = gd.getWatchDog(0);
    gd.force_check();
    time_point_ += std::chrono::milliseconds(500);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    gd.force_check();
  }

  void startNeglectedDogsAndWait() {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    GuardDogImpl gd(fakestats_, config_multikill_, time_source_);
    auto unpet_dog1 = gd.getWatchDog(0);
    gd.force_check();
    auto unpet_dog2 = gd.getWatchDog(1);
    gd.force_check();
    time_point_ += std::chrono::milliseconds(501);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    gd.force_check();
  }

  void startNeglectedDogAndTouchedDog() {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    GuardDogImpl gd(fakestats_, config_multikill_, time_source_);
    auto unpet_dog = gd.getWatchDog(0);
    auto pet_dog = gd.getWatchDog(1);
    for (int i = 0; i < 6; i++) {
      time_point_ += std::chrono::milliseconds(100);
      EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
      pet_dog->touch();
      gd.force_check();
    }
  }

  testing::NiceMock<Server::Configuration::MockMain> config_kill_;
  testing::NiceMock<Server::Configuration::MockMain> config_multikill_;
  testing::NiceMock<Stats::MockStore> fakestats_;
  MockSystemTimeSource time_source_;
  std::chrono::system_clock::time_point time_point_;
};

class GuardDogMissTest : public testing::Test {
protected:
  GuardDogMissTest() : config_miss(1, 1000, 0, 0), config_mega(1000, 1, 0, 0) {}

  void startNeglectedDogAndWait(bool mega) {
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    GuardDogImpl gd(stats_store, mega ? config_mega : config_miss, time_source_);
    auto unpet_dog = gd.getWatchDog(0);
    time_point_ += std::chrono::milliseconds(501);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    gd.force_check();
    gd.stopWatching(unpet_dog);
    unpet_dog = nullptr;
  }

  testing::NiceMock<Server::Configuration::MockMain> config_miss;
  testing::NiceMock<Server::Configuration::MockMain> config_mega;
  testing::NiceMock<Stats::MockStore> stats_store;
  MockSystemTimeSource time_source_;
  std::chrono::system_clock::time_point time_point_;
};

TEST_F(GuardDogDeathTest, KillDeathTest) { EXPECT_DEATH(startNeglectedDogAndWait(), ""); }
TEST_F(GuardDogDeathTest, MultiKillDeathTest) { EXPECT_DEATH(startNeglectedDogsAndWait(), ""); }

TEST_F(GuardDogDeathTest, NearDeathTest) {
  startNeglectedDogAndTouchedDog();
  SUCCEED(); // if we haven't crashed by now
}

TEST_F(GuardDogMissTest, MissTest) {
  startNeglectedDogAndWait(false);
  SUCCEED(); // if we haven't crashed by now
}

TEST_F(GuardDogMissTest, MegaMissTest) {
  startNeglectedDogAndWait(true);
  SUCCEED(); // if we haven't crashed by now
}

TEST(GuardDogBasicTest, StartStopTest) {
  testing::NiceMock<Stats::MockStore> stats;
  testing::NiceMock<Server::Configuration::MockMain> config(0, 0, 0, 0);
  testing::NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
}

TEST(GuardDogBasicTest, LoopIntervalNoKillTest) {
  testing::NiceMock<Stats::MockStore> stats;
  testing::NiceMock<Server::Configuration::MockMain> config(40, 50, 0, 0);
  testing::NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  ASSERT_EQ(gd.loopInterval(), 40);
}

TEST(GuardDogBasicTest, LoopIntervalTest) {
  testing::NiceMock<Stats::MockStore> stats;
  testing::NiceMock<Server::Configuration::MockMain> config(100, 90, 1000, 500);
  testing::NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  ASSERT_EQ(gd.loopInterval(), 90);
}

TEST(WatchDogBasicTest, ThreadIdTest) {
  testing::NiceMock<Stats::MockStore> stats;
  testing::NiceMock<Server::Configuration::MockMain> config(100, 90, 1000, 500);
  testing::NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  auto watched_dog = gd.getWatchDog(123);
  ASSERT_EQ(watched_dog->threadId(), 123);
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

} // Event
