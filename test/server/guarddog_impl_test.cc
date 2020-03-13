#include <atomic>
#include <chrono>
#include <memory>

#include "envoy/common/time.h"

#include "common/api/api_impl.h"
#include "common/common/macros.h"
#include "common/common/utility.h"

#include "server/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Server {
namespace {

class DebugTestInterlock : public GuardDogImpl::TestInterlockHook {
public:
  // GuardDogImpl::TestInterlockHook
  void signalFromImpl(MonotonicTime time) override {
    impl_reached_ = time;
    impl_.notifyAll();
  }

  void waitFromTest(Thread::MutexBasicLockable& mutex, MonotonicTime time) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    while (impl_reached_ < time) {
      impl_.wait(mutex);
    }
  }

private:
  Thread::CondVar impl_;
  MonotonicTime impl_reached_;
};

// We want to make sure guard-dog is tested with both simulated time and real
// time, to ensure that it works in production, and that it works in the context
// of integration tests which are much easier to control with simulated time.
enum class TimeSystemType { Real, Simulated };

class GuardDogTestBase : public testing::TestWithParam<TimeSystemType> {
protected:
  GuardDogTestBase()
      : time_system_(makeTimeSystem()), api_(Api::createApiForTest(stats_store_, *time_system_)) {}

  static std::unique_ptr<Event::TestTimeSystem> makeTimeSystem() {
    if (GetParam() == TimeSystemType::Real) {
      return std::make_unique<Event::GlobalTimeSystem>();
    }
    ASSERT(GetParam() == TimeSystemType::Simulated);
    return std::make_unique<Event::SimulatedTimeSystem>();
  }

  void initGuardDog(Stats::Scope& stats_scope, const Server::Configuration::Main& config) {
    guard_dog_ = std::make_unique<GuardDogImpl>(stats_scope, config, *api_,
                                                std::make_unique<DebugTestInterlock>());
  }

  std::unique_ptr<Event::TestTimeSystem> time_system_;
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  std::unique_ptr<GuardDogImpl> guard_dog_;
};

INSTANTIATE_TEST_SUITE_P(TimeSystemType, GuardDogTestBase,
                         testing::ValuesIn({TimeSystemType::Real, TimeSystemType::Simulated}));

/**
 * Death test caveat: Because of the way we die gcov doesn't receive coverage
 * information from the forked process that is checked for successful death.
 * This means that the lines dealing with the calls to PANIC are not seen as
 * green in the coverage report. However, rest assured from the results of the
 * test: these lines are in fact covered.
 */
class GuardDogDeathTest : public GuardDogTestBase {
protected:
  GuardDogDeathTest()
      : config_kill_(1000, 1000, 100, 1000), config_multikill_(1000, 1000, 1000, 500) {}

  /**
   * This does everything but the final forceCheckForTest() that should cause
   * death for the single kill case.
   */
  void SetupForDeath() {
    InSequence s;
    initGuardDog(fakestats_, config_kill_);
    unpet_dog_ = guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
    guard_dog_->forceCheckForTest();
    time_system_->sleep(std::chrono::milliseconds(99)); // 1 ms shy of death.
  }

  /**
   * This does everything but the final forceCheckForTest() that should cause
   * death for the multiple kill case.
   */
  void SetupForMultiDeath() {
    InSequence s;
    initGuardDog(fakestats_, config_multikill_);
    auto unpet_dog_ =
        guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
    guard_dog_->forceCheckForTest();
    auto second_dog_ =
        guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
    guard_dog_->forceCheckForTest();
    time_system_->sleep(std::chrono::milliseconds(499)); // 1 ms shy of multi-death.
  }

  NiceMock<Configuration::MockMain> config_kill_;
  NiceMock<Configuration::MockMain> config_multikill_;
  NiceMock<Stats::MockStore> fakestats_;
  WatchDogSharedPtr unpet_dog_;
  WatchDogSharedPtr second_dog_;
};

INSTANTIATE_TEST_SUITE_P(TimeSystemType, GuardDogDeathTest,
                         testing::ValuesIn({TimeSystemType::Real, TimeSystemType::Simulated}));

// These tests use threads, and need to run after the real death tests, so we need to call them
// a different name.
class GuardDogAlmostDeadTest : public GuardDogDeathTest {};

INSTANTIATE_TEST_SUITE_P(
    TimeSystemType, GuardDogAlmostDeadTest,
    testing::ValuesIn({// TODO(#6464): TimeSystemType::Real -- fails in this suite 30/1000 times.
                       TimeSystemType::Simulated}));

TEST_P(GuardDogDeathTest, KillDeathTest) {
  // Is it German for "The Function"? Almost...
  auto die_function = [&]() -> void {
    SetupForDeath();
    time_system_->sleep(std::chrono::milliseconds(401)); // 400 ms past death.
    guard_dog_->forceCheckForTest();
  };

  // Why do it this way? Any threads must be started inside the death test
  // statement and this is the easiest way to accomplish that.
  EXPECT_DEATH(die_function(), "");
}

TEST_P(GuardDogAlmostDeadTest, KillNoFinalCheckTest) {
  // This does everything the death test does, except allow enough time to
  // expire to reach the death panic. The death test does not verify that there
  // was not a crash *before* the expected line, so this test checks that.
  SetupForDeath();
}

TEST_P(GuardDogDeathTest, MultiKillDeathTest) {
  auto die_function = [&]() -> void {
    SetupForMultiDeath();
    time_system_->sleep(std::chrono::milliseconds(2)); // 1 ms past multi-death.
    guard_dog_->forceCheckForTest();
  };
  EXPECT_DEATH(die_function(), "");
}

TEST_P(GuardDogAlmostDeadTest, MultiKillNoFinalCheckTest) {
  // This does everything the death test does not except the final force check that
  // should actually result in dying. The death test does not verify that there
  // was not a crash *before* the expected line, so this test checks that.
  SetupForMultiDeath();
}

TEST_P(GuardDogAlmostDeadTest, NearDeathTest) {
  // This ensures that if only one thread surpasses the multiple kill threshold
  // there is no death. The positive case is covered in MultiKillDeathTest.
  InSequence s;
  initGuardDog(fakestats_, config_multikill_);
  auto unpet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  auto pet_dog = guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  // This part "waits" 600 milliseconds while one dog is touched every 100, and
  // the other is not. 600ms is over the threshold of 500ms for multi-kill but
  // only one is nonresponsive, so there should be no kill (single kill
  // threshold of 1s is not reached).
  for (int i = 0; i < 6; i++) {
    time_system_->sleep(std::chrono::milliseconds(100));
    pet_dog->touch();
    guard_dog_->forceCheckForTest();
  }
}

class GuardDogMissTest : public GuardDogTestBase {
protected:
  GuardDogMissTest() : config_miss_(500, 1000, 0, 0), config_mega_(1000, 500, 0, 0) {}

  void checkMiss(uint64_t count, const std::string& descriptor) {
    EXPECT_EQ(count, TestUtility::findCounter(stats_store_, "server.watchdog_miss")->value())
        << descriptor;
    EXPECT_EQ(count,
              TestUtility::findCounter(stats_store_, "server.test_thread.watchdog_miss")->value())
        << descriptor;
  }

  void checkMegaMiss(uint64_t count, const std::string& descriptor) {
    EXPECT_EQ(count, TestUtility::findCounter(stats_store_, "server.watchdog_mega_miss")->value())
        << descriptor;
    EXPECT_EQ(
        count,
        TestUtility::findCounter(stats_store_, "server.test_thread.watchdog_mega_miss")->value())
        << descriptor;
  }

  NiceMock<Configuration::MockMain> config_miss_;
  NiceMock<Configuration::MockMain> config_mega_;
};

INSTANTIATE_TEST_SUITE_P(TimeSystemType, GuardDogMissTest,
                         testing::ValuesIn({TimeSystemType::Real, TimeSystemType::Simulated}));

TEST_P(GuardDogMissTest, MissTest) {
  // This test checks the actual collected statistics after doing some timer
  // advances that should and shouldn't increment the counters.
  initGuardDog(stats_store_, config_miss_);
  auto unpet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  // We'd better start at 0:
  checkMiss(0, "MissTest check 1");
  // At 300ms we shouldn't have hit the timeout yet:
  time_system_->sleep(std::chrono::milliseconds(300));
  guard_dog_->forceCheckForTest();
  checkMiss(0, "MissTest check 2");
  // This should push it past the 500ms limit:
  time_system_->sleep(std::chrono::milliseconds(250));
  guard_dog_->forceCheckForTest();
  checkMiss(1, "MissTest check 3");
  guard_dog_->stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST_P(GuardDogMissTest, MegaMissTest) {
  // TODO(#6464): This test fails in real-time 1/1000 times, but passes in simulated time.
  if (GetParam() == TimeSystemType::Real) {
    return;
  }

  // This test checks the actual collected statistics after doing some timer
  // advances that should and shouldn't increment the counters.
  initGuardDog(stats_store_, config_mega_);
  auto unpet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  // We'd better start at 0:
  checkMegaMiss(0, "MegaMissTest check 1");
  // This shouldn't be enough to increment the stat:
  time_system_->sleep(std::chrono::milliseconds(499));
  guard_dog_->forceCheckForTest();
  checkMegaMiss(0, "MegaMissTest check 2");
  // Just 2ms more will make it greater than 500ms timeout:
  time_system_->sleep(std::chrono::milliseconds(2));
  guard_dog_->forceCheckForTest();
  checkMegaMiss(1, "MegaMissTest check 3");
  guard_dog_->stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST_P(GuardDogMissTest, MissCountTest) {
  // TODO(#6464): This test fails in real-time 9/1000 times, but passes in simulated time.
  if (GetParam() == TimeSystemType::Real) {
    return;
  }

  // This tests a flake discovered in the MissTest where real timeout or
  // spurious condition_variable wakeup causes the counter to get incremented
  // more than it should be.
  initGuardDog(stats_store_, config_miss_);
  auto sometimes_pet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  // These steps are executed once without ever touching the watchdog.
  // Then the last step is to touch the watchdog and repeat the steps.
  // This verifies that the behavior is reset back to baseline after a touch.
  for (unsigned long i = 0; i < 2; i++) {
    EXPECT_EQ(i, stats_store_.counter("server.watchdog_miss").value());
    // This shouldn't be enough to increment the stat:
    time_system_->sleep(std::chrono::milliseconds(499));
    guard_dog_->forceCheckForTest();
    checkMiss(i, "MissCountTest check 1");
    // And if we force re-execution of the loop it still shouldn't be:
    guard_dog_->forceCheckForTest();
    checkMiss(i, "MissCountTest check 2");
    // Just 2ms more will make it greater than 500ms timeout:
    time_system_->sleep(std::chrono::milliseconds(2));
    guard_dog_->forceCheckForTest();
    checkMiss(i + 1, "MissCountTest check 3");
    // Spurious wakeup, we should still only have one miss counted.
    guard_dog_->forceCheckForTest();
    checkMiss(i + 1, "MissCountTest check 4");
    // When we finally touch the dog we should get one more increment once the
    // timeout value expires:
    sometimes_pet_dog->touch();
  }
  time_system_->sleep(std::chrono::milliseconds(1000));
  sometimes_pet_dog->touch();
  // Make sure megamiss still works:
  checkMegaMiss(0UL, "MissCountTest check 5");
  time_system_->sleep(std::chrono::milliseconds(1500));
  guard_dog_->forceCheckForTest();
  checkMegaMiss(1UL, "MissCountTest check 6");

  guard_dog_->stopWatching(sometimes_pet_dog);
  sometimes_pet_dog = nullptr;
}

TEST_P(GuardDogTestBase, StartStopTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(0, 0, 0, 0);
  initGuardDog(stats, config);
}

TEST_P(GuardDogTestBase, LoopIntervalNoKillTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(40, 50, 0, 0);
  initGuardDog(stats, config);
  EXPECT_EQ(guard_dog_->loopIntervalForTest(), std::chrono::milliseconds(40));
}

TEST_P(GuardDogTestBase, LoopIntervalTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(100, 90, 1000, 500);
  initGuardDog(stats, config);
  EXPECT_EQ(guard_dog_->loopIntervalForTest(), std::chrono::milliseconds(90));
}

TEST_P(GuardDogTestBase, WatchDogThreadIdTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(100, 90, 1000, 500);
  initGuardDog(stats, config);
  auto watched_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  EXPECT_EQ(watched_dog->threadId().debugString(),
            api_->threadFactory().currentThreadId().debugString());
  guard_dog_->stopWatching(watched_dog);
}

// If this test fails it is because the std::chrono::steady_clock::duration type has become
// nontrivial or we are compiling under a compiler and library combo that makes
// std::chrono::steady_clock::duration require a lock to be atomically modified.
//
// The WatchDog/GuardDog relies on this being a lock free atomic for perf reasons so some workaround
// will be required if this test starts failing.
TEST_P(GuardDogTestBase, AtomicIsAtomicTest) {
  std::atomic<std::chrono::steady_clock::duration> atomic_time;
  ASSERT_EQ(atomic_time.is_lock_free(), true);
}

} // namespace
} // namespace Server
} // namespace Envoy
