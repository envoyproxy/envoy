#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/server/configuration.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/server/watchdog.h"
#include "envoy/thread/thread.h"

#include "common/api/api_impl.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/protobuf/utility.h"

#include "server/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/watchdog_config.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Server {
namespace {

// Kill has an explicit value that disables the feature.
const int DISABLE_KILL = 0;
const int DISABLE_MULTIKILL = 0;

// Miss / Megamiss don't have an explicit value that disables them
// so set a timeout larger than those used in tests for 'disable' it.
const int DISABLE_MISS = 1000000;
const int DISABLE_MEGAMISS = 1000000;

class DebugTestInterlock : public GuardDogImpl::TestInterlockHook {
public:
  // GuardDogImpl::TestInterlockHook
  void signalFromImpl() override {
    waiting_for_signal_ = false;
    impl_.notifyAll();
  }

  void waitFromTest(Thread::MutexBasicLockable& mutex) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    ASSERT(!waiting_for_signal_);
    waiting_for_signal_ = true;
    while (waiting_for_signal_) {
      impl_.wait(mutex);
    }
  }

private:
  Thread::CondVar impl_;
  bool waiting_for_signal_ = false;
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

  void initGuardDog(Stats::Scope& stats_scope, const Server::Configuration::Watchdog& config) {
    guard_dog_ = std::make_unique<GuardDogImpl>(stats_scope, config, *api_, "server",
                                                std::make_unique<DebugTestInterlock>());
  }

  std::unique_ptr<Event::TestTimeSystem> time_system_;
  Stats::TestUtil::TestStore stats_store_;
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
      : config_kill_(1000, 1000, 100, 1000, 0, std::vector<std::string>{}),
        config_multikill_(1000, 1000, 1000, 500, 0, std::vector<std::string>{}),
        config_multikill_threshold_(1000, 1000, 1000, 500, 60, std::vector<std::string>{}) {}

  /**
   * This does everything but the final forceCheckForTest() that should cause
   * death for the single kill case.
   */
  void SetupForDeath() {
    InSequence s;
    initGuardDog(fakestats_, config_kill_);
    unpet_dog_ = guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
    dogs_.emplace_back(unpet_dog_);
    guard_dog_->forceCheckForTest();
    time_system_->advanceTimeWait(std::chrono::milliseconds(99)); // 1 ms shy of death.
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
    dogs_.emplace_back(unpet_dog_);
    guard_dog_->forceCheckForTest();
    auto second_dog_ =
        guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
    dogs_.emplace_back(second_dog_);
    guard_dog_->forceCheckForTest();
    time_system_->advanceTimeWait(std::chrono::milliseconds(499)); // 1 ms shy of multi-death.
  }

  /**
   * This does everything but the final forceCheckForTest() that should cause
   * death for the multiple kill case using threshold (100% of watchdogs over the threshold).
   */
  void setupForMultiDeathThreshold() {
    InSequence s;
    initGuardDog(fakestats_, config_multikill_threshold_);

    // Creates 5 watchdogs.
    for (int i = 0; i < 5; ++i) {
      auto dog = guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
      dogs_.emplace_back(dog);

      if (i == 0) {
        unpet_dog_ = dog;
      } else if (i == 1) {
        second_dog_ = dog;
      }

      guard_dog_->forceCheckForTest();
    }

    time_system_->advanceTimeWait(std::chrono::milliseconds(499)); // 1 ms shy of multi-death.
  }

  NiceMock<Configuration::MockWatchdog> config_kill_;
  NiceMock<Configuration::MockWatchdog> config_multikill_;
  NiceMock<Configuration::MockWatchdog> config_multikill_threshold_;
  NiceMock<Stats::MockStore> fakestats_;
  WatchDogSharedPtr unpet_dog_;
  WatchDogSharedPtr second_dog_;
  std::vector<WatchDogSharedPtr> dogs_; // Tracks all watchdogs created.
};

INSTANTIATE_TEST_SUITE_P(TimeSystemType, GuardDogDeathTest,
                         testing::ValuesIn({TimeSystemType::Real, TimeSystemType::Simulated}));

// These tests use threads, and need to run after the real death tests, so we need to call them
// a different name.
class GuardDogAlmostDeadTest : public GuardDogDeathTest {};

INSTANTIATE_TEST_SUITE_P(
    TimeSystemType, GuardDogAlmostDeadTest,
    testing::ValuesIn({// TODO(#6465): TimeSystemType::Real -- fails in this suite 30/1000 times.
                       TimeSystemType::Simulated}));

TEST_P(GuardDogDeathTest, KillDeathTest) {
  // Is it German for "The Function"? Almost...
  auto die_function = [&]() -> void {
    SetupForDeath();
    time_system_->advanceTimeWait(std::chrono::milliseconds(401)); // 400 ms past death.
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
    time_system_->advanceTimeWait(std::chrono::milliseconds(2)); // 1 ms past multi-death.
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

TEST_P(GuardDogDeathTest, MultiKillThresholdDeathTest) {
  auto die_function = [&]() -> void {
    setupForMultiDeathThreshold();

    // Pet the last two dogs so we're just at the threshold that causes death.
    dogs_.at(4)->touch();
    dogs_.at(3)->touch();

    time_system_->advanceTimeWait(std::chrono::milliseconds(2)); // 1 ms past multi-death.
    guard_dog_->forceCheckForTest();
  };
  EXPECT_DEATH(die_function(), "");
}

TEST_P(GuardDogAlmostDeadTest, MultiKillUnderThreshold) {
  // This does everything the death test does except it pets an additional watchdog
  // that causes us to be under the threshold (60%) of multikill death.
  setupForMultiDeathThreshold();

  // Pet the last three dogs so we're just under the threshold that causes death.
  dogs_.at(4)->touch();
  dogs_.at(3)->touch();
  dogs_.at(2)->touch();

  time_system_->advanceTimeWait(std::chrono::milliseconds(2)); // 1 ms past multi-death.
  guard_dog_->forceCheckForTest();
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
    time_system_->advanceTimeWait(std::chrono::milliseconds(100));
    pet_dog->touch();
    guard_dog_->forceCheckForTest();
  }
}

class GuardDogMissTest : public GuardDogTestBase {
protected:
  GuardDogMissTest()
      : config_miss_(500, 1000, 0, 0, 0, std::vector<std::string>{}),
        config_mega_(1000, 500, 0, 0, 0, std::vector<std::string>{}) {}

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

  NiceMock<Configuration::MockWatchdog> config_miss_;
  NiceMock<Configuration::MockWatchdog> config_mega_;
};

INSTANTIATE_TEST_SUITE_P(TimeSystemType, GuardDogMissTest,
                         testing::ValuesIn({TimeSystemType::Real, TimeSystemType::Simulated}));

TEST_P(GuardDogMissTest, MissTest) {
  // This test checks the actual collected statistics after doing some timer
  // advances that should and shouldn't increment the counters.
  initGuardDog(stats_store_, config_miss_);
  auto unpet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  guard_dog_->forceCheckForTest();
  // We'd better start at 0:
  checkMiss(0, "MissTest check 1");
  // At 300ms we shouldn't have hit the timeout yet:
  time_system_->advanceTimeWait(std::chrono::milliseconds(300));
  guard_dog_->forceCheckForTest();
  checkMiss(0, "MissTest check 2");
  // This should push it past the 500ms limit:
  time_system_->advanceTimeWait(std::chrono::milliseconds(250));
  guard_dog_->forceCheckForTest();
  checkMiss(1, "MissTest check 3");
  guard_dog_->stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST_P(GuardDogMissTest, MegaMissTest) {
  // TODO(#6465): This test fails in real-time 1/1000 times, but passes in simulated time.
  if (GetParam() == TimeSystemType::Real) {
    return;
  }

  // This test checks the actual collected statistics after doing some timer
  // advances that should and shouldn't increment the counters.
  initGuardDog(stats_store_, config_mega_);
  auto unpet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  guard_dog_->forceCheckForTest();
  // We'd better start at 0:
  checkMegaMiss(0, "MegaMissTest check 1");
  // This shouldn't be enough to increment the stat:
  time_system_->advanceTimeWait(std::chrono::milliseconds(499));
  guard_dog_->forceCheckForTest();
  checkMegaMiss(0, "MegaMissTest check 2");
  // Just 2ms more will make it greater than 500ms timeout:
  time_system_->advanceTimeWait(std::chrono::milliseconds(2));
  guard_dog_->forceCheckForTest();
  checkMegaMiss(1, "MegaMissTest check 3");
  guard_dog_->stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST_P(GuardDogMissTest, MissCountTest) {
  // TODO(#6465): This test fails in real-time 9/1000 times, but passes in simulated time.
  if (GetParam() == TimeSystemType::Real) {
    return;
  }

  // This tests a flake discovered in the MissTest where real timeout or
  // spurious condition_variable wakeup causes the counter to get incremented
  // more than it should be.
  initGuardDog(stats_store_, config_miss_);
  auto sometimes_pet_dog =
      guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(), "test_thread");
  guard_dog_->forceCheckForTest();
  // These steps are executed once without ever touching the watchdog.
  // Then the last step is to touch the watchdog and repeat the steps.
  // This verifies that the behavior is reset back to baseline after a touch.
  for (unsigned long i = 0; i < 2; i++) {
    EXPECT_EQ(i, stats_store_.counter("server.watchdog_miss").value());
    // This shouldn't be enough to increment the stat:
    time_system_->advanceTimeWait(std::chrono::milliseconds(499));
    guard_dog_->forceCheckForTest();
    checkMiss(i, "MissCountTest check 1");
    // And if we force re-execution of the loop it still shouldn't be:
    guard_dog_->forceCheckForTest();
    checkMiss(i, "MissCountTest check 2");
    // Just 2ms more will make it greater than 500ms timeout:
    time_system_->advanceTimeWait(std::chrono::milliseconds(2));
    guard_dog_->forceCheckForTest();
    checkMiss(i + 1, "MissCountTest check 3");
    // Spurious wakeup, we should still only have one miss counted.
    guard_dog_->forceCheckForTest();
    checkMiss(i + 1, "MissCountTest check 4");
    // When we finally touch the dog we should get one more increment once the
    // timeout value expires:
    sometimes_pet_dog->touch();
    guard_dog_->forceCheckForTest();
  }
  time_system_->advanceTimeWait(std::chrono::milliseconds(1000));
  sometimes_pet_dog->touch();
  guard_dog_->forceCheckForTest();
  // Make sure megamiss still works:
  checkMegaMiss(0UL, "MissCountTest check 5");
  time_system_->advanceTimeWait(std::chrono::milliseconds(1500));
  guard_dog_->forceCheckForTest();
  checkMegaMiss(1UL, "MissCountTest check 6");

  guard_dog_->stopWatching(sometimes_pet_dog);
  sometimes_pet_dog = nullptr;
}

TEST_P(GuardDogTestBase, StartStopTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockWatchdog> config(0, 0, 0, 0, 0, std::vector<std::string>{});
  initGuardDog(stats, config);
}

TEST_P(GuardDogTestBase, LoopIntervalNoKillTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockWatchdog> config(40, 50, 0, 0, 0, std::vector<std::string>{});
  initGuardDog(stats, config);
  EXPECT_EQ(guard_dog_->loopIntervalForTest(), std::chrono::milliseconds(40));
}

TEST_P(GuardDogTestBase, LoopIntervalTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockWatchdog> config(100, 90, 1000, 500, 0, std::vector<std::string>{});
  initGuardDog(stats, config);
  EXPECT_EQ(guard_dog_->loopIntervalForTest(), std::chrono::milliseconds(90));
}

TEST_P(GuardDogTestBase, WatchDogThreadIdTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockWatchdog> config(100, 90, 1000, 500, 0, std::vector<std::string>{});
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

// A GuardDogAction used for testing the GuardDog.
// It's primary use is dumping string of the format EVENT_TYPE : tid1,.., tidN to
// the events vector passed to it.
// Instances of this class will be registered for GuardDogEvent through
// TestGuardDogActionFactory.
class RecordGuardDogAction : public Configuration::GuardDogAction {
public:
  RecordGuardDogAction(std::vector<std::string>& events) : events_(events) {}

  void run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent event,
           const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_last_checkin_pairs,
           MonotonicTime /*now*/) override {
    std::string event_string =
        envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent_Name(event);
    absl::StrAppend(&event_string, " : ");
    std::vector<std::string> output_string_parts;
    output_string_parts.reserve(thread_last_checkin_pairs.size());

    for (const auto& thread_ltt_pair : thread_last_checkin_pairs) {
      output_string_parts.push_back(thread_ltt_pair.first.debugString());
    }

    absl::StrAppend(&event_string, absl::StrJoin(output_string_parts, ","));
    events_.push_back(event_string);
  }

protected:
  std::vector<std::string>& events_; // not owned
};

// A GuardDogAction that raises the specified signal.
class AssertGuardDogAction : public Configuration::GuardDogAction {
public:
  AssertGuardDogAction() = default;

  void
  run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent /*event*/,
      const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& /*thread_last_checkin_pairs*/,
      MonotonicTime /*now*/) override {
    RELEASE_ASSERT(false, "ASSERT_GUARDDOG_ACTION");
  }
};

// Test factory for consuming Watchdog configs and creating GuardDogActions.
template <class ConfigType>
class RecordGuardDogActionFactory : public Configuration::GuardDogActionFactory {
public:
  RecordGuardDogActionFactory(const std::string& name, std::vector<std::string>& events)
      : name_(name), events_(events) {}

  Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& /*config*/,
      Configuration::GuardDogActionFactoryContext& /*context*/) override {
    // Return different actions depending on the config.
    return std::make_unique<RecordGuardDogAction>(events_);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigType()};
  }

  std::string name() const override { return name_; }

  const std::string name_;
  std::vector<std::string>& events_; // not owned
};

// Test factory for consuming Watchdog configs and creating GuardDogActions.
template <class ConfigType>
class AssertGuardDogActionFactory : public Configuration::GuardDogActionFactory {
public:
  AssertGuardDogActionFactory(const std::string& name) : name_(name) {}

  Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& /*config*/,
      Configuration::GuardDogActionFactoryContext& /*context*/) override {
    // Return different actions depending on the config.
    return std::make_unique<AssertGuardDogAction>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigType()};
  }

  std::string name() const override { return name_; }

  const std::string name_;
};

/**
 * Tests that various actions registered for the guard dog get called upon.
 */
class GuardDogActionsTest : public GuardDogTestBase {
protected:
  GuardDogActionsTest()
      : log_factory_("LogFactory", events_), register_log_factory_(log_factory_),
        assert_factory_("AssertFactory"), register_assert_factory_(assert_factory_) {}

  std::vector<std::string> getActionsConfig() {
    return {
        R"EOF(
        {
          "config": {
            "name": "AssertFactory",
            "typed_config": {
              "@type": "type.googleapis.com/google.protobuf.Empty"
            }
          },
          "event": "MULTIKILL"
        }
      )EOF",
        R"EOF(
        {
          "config": {
            "name": "AssertFactory",
            "typed_config": {
              "@type": "type.googleapis.com/google.protobuf.Empty"
            }
          },
          "event": "KILL"
        }
      )EOF",
        R"EOF(
        {
          "config": {
            "name": "LogFactory",
            "typed_config": {
              "@type": "type.googleapis.com/google.protobuf.Empty"
            }
          },
          "event": "MEGAMISS"
        }
      )EOF",
        R"EOF(
        {
          "config": {
            "name": "LogFactory",
            "typed_config": {
              "@type": "type.googleapis.com/google.protobuf.Empty"
            }
          },
          "event": "MISS"
        }
      )EOF"};
  }

  void setupFirstDog(const NiceMock<Configuration::MockWatchdog>& config, Thread::ThreadId tid) {
    initGuardDog(fake_stats_, config);
    first_dog_ = guard_dog_->createWatchDog(tid, "test_thread");
    guard_dog_->forceCheckForTest();
  }

  std::vector<std::string> actions_;
  std::vector<std::string> events_;
  RecordGuardDogActionFactory<Envoy::ProtobufWkt::Empty> log_factory_;
  Registry::InjectFactory<Configuration::GuardDogActionFactory> register_log_factory_;
  AssertGuardDogActionFactory<Envoy::ProtobufWkt::Empty> assert_factory_;
  Registry::InjectFactory<Configuration::GuardDogActionFactory> register_assert_factory_;
  NiceMock<Stats::MockStore> fake_stats_;
  WatchDogSharedPtr first_dog_;
  WatchDogSharedPtr second_dog_;
};

INSTANTIATE_TEST_SUITE_P(TimeSystemType, GuardDogActionsTest,
                         testing::ValuesIn({TimeSystemType::Real, TimeSystemType::Simulated}));

TEST_P(GuardDogActionsTest, MissShouldOnlyReportRelevantThreads) {
  const NiceMock<Configuration::MockWatchdog> config(100, DISABLE_MEGAMISS, DISABLE_KILL,
                                                     DISABLE_MULTIKILL, 0, getActionsConfig());
  setupFirstDog(config, Thread::ThreadId(10));
  second_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");
  time_system_->advanceTimeWait(std::chrono::milliseconds(50));
  second_dog_->touch();

  // This will reset the loop interval timer, and should help us
  // synchronize with the guard dog.
  guard_dog_->forceCheckForTest();

  time_system_->advanceTimeWait(std::chrono::milliseconds(51));
  guard_dog_->forceCheckForTest();

  EXPECT_THAT(events_, ElementsAre("MISS : 10"));
}

TEST_P(GuardDogActionsTest, MissShouldBeAbleToReportMultipleThreads) {
  const NiceMock<Configuration::MockWatchdog> config(100, DISABLE_MEGAMISS, DISABLE_KILL,
                                                     DISABLE_MULTIKILL, 0, getActionsConfig());
  initGuardDog(fake_stats_, config);
  first_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(10), "test_thread");
  second_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");

  first_dog_->touch();
  second_dog_->touch();
  // This should ensure that when the next call to step() occurs, both of the
  // dogs will be over last touch time threshold and be reported in the event.
  // The next call to step() will either be triggered by the timer or after
  // advanceTimeWait() below, but only one of them will append to events_
  // because of saturation.
  guard_dog_->forceCheckForTest();

  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MISS : 10,11"));
}

TEST_P(GuardDogActionsTest, MissShouldSaturateOnMissEvent) {
  const NiceMock<Configuration::MockWatchdog> config(100, DISABLE_MISS, DISABLE_KILL,
                                                     DISABLE_MULTIKILL, 0, getActionsConfig());
  setupFirstDog(config, Thread::ThreadId(10));

  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MISS : 10"));

  // Should saturate and not add an additional "event_"
  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MISS : 10"));

  // Touch the watchdog, which should allow the event to trigger again.
  first_dog_->touch();
  guard_dog_->forceCheckForTest();

  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MISS : 10", "MISS : 10"));
}

TEST_P(GuardDogActionsTest, MegaMissShouldOnlyReportRelevantThreads) {
  const NiceMock<Configuration::MockWatchdog> config(DISABLE_MISS, 100, DISABLE_KILL,
                                                     DISABLE_MULTIKILL, 0, getActionsConfig());
  setupFirstDog(config, Thread::ThreadId(10));
  second_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");
  time_system_->advanceTimeWait(std::chrono::milliseconds(50));
  second_dog_->touch();

  // This will reset the loop interval timer, and should help us
  // synchronize with the guard dog.
  guard_dog_->forceCheckForTest();

  time_system_->advanceTimeWait(std::chrono::milliseconds(51));
  guard_dog_->forceCheckForTest();

  EXPECT_THAT(events_, ElementsAre("MEGAMISS : 10"));
}

TEST_P(GuardDogActionsTest, MegaMissShouldBeAbleToReportMultipleThreads) {
  const NiceMock<Configuration::MockWatchdog> config(DISABLE_MISS, 100, DISABLE_KILL,
                                                     DISABLE_MULTIKILL, 0, getActionsConfig());
  initGuardDog(fake_stats_, config);
  first_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(10), "test_thread");
  second_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");

  first_dog_->touch();
  second_dog_->touch();

  // This should ensure that when the next call to step() occurs, both of the
  // dogs will be over last touch time threshold and be reported in the event.
  // The next call to step() will either be triggered by the timer or after
  // advanceTimeWait() below, but only one of them will append to events_
  // because of saturation.
  guard_dog_->forceCheckForTest();
  time_system_->advanceTimeWait(std::chrono::milliseconds(101));

  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MEGAMISS : 10,11"));
}

TEST_P(GuardDogActionsTest, MegaMissShouldSaturateOnMegaMissEvent) {
  const NiceMock<Configuration::MockWatchdog> config(DISABLE_MISS, 100, DISABLE_KILL,
                                                     DISABLE_MULTIKILL, 0, getActionsConfig());
  setupFirstDog(config, Thread::ThreadId(10));

  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MEGAMISS : 10"));

  // Should saturate and not add an additional "event_"
  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MEGAMISS : 10"));

  // Touch the watchdog, which should allow the event to trigger again.
  first_dog_->touch();
  guard_dog_->forceCheckForTest();

  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MEGAMISS : 10", "MEGAMISS : 10"));
}

TEST_P(GuardDogActionsTest, ShouldRespectEventPriority) {
  // Priority of events are KILL, MULTIKILL, MEGAMISS and MISS

  // Kill event should fire before the others
  auto kill_function = [&]() -> void {
    const NiceMock<Configuration::MockWatchdog> config(100, 100, 100, 100, 0, getActionsConfig());
    initGuardDog(fake_stats_, config);
    auto first_dog = guard_dog_->createWatchDog(Thread::ThreadId(10), "test_thread");
    auto second_dog = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");
    guard_dog_->forceCheckForTest();
    time_system_->advanceTimeWait(std::chrono::milliseconds(101));
    guard_dog_->forceCheckForTest();
  };

  // We expect only the kill action to have fired
  EXPECT_DEATH(kill_function(), "ASSERT_GUARDDOG_ACTION");

  // Multikill event should fire before the others
  auto multikill_function = [&]() -> void {
    const NiceMock<Configuration::MockWatchdog> config(100, 100, DISABLE_KILL, 100, 0,
                                                       getActionsConfig());
    initGuardDog(fake_stats_, config);
    auto first_dog = guard_dog_->createWatchDog(Thread::ThreadId(10), "test_thread");
    auto second_dog = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");
    guard_dog_->forceCheckForTest();
    time_system_->advanceTimeWait(std::chrono::milliseconds(101));
    guard_dog_->forceCheckForTest();
  };

  EXPECT_DEATH(multikill_function(), "ASSERT_GUARDDOG_ACTION");

  // We expect megamiss to fire before miss
  const NiceMock<Configuration::MockWatchdog> config(100, 100, DISABLE_KILL, DISABLE_MULTIKILL, 0,
                                                     getActionsConfig());
  setupFirstDog(config, Thread::ThreadId(10));
  time_system_->advanceTimeWait(std::chrono::milliseconds(101));
  guard_dog_->forceCheckForTest();
  EXPECT_THAT(events_, ElementsAre("MEGAMISS : 10", "MISS : 10"));
}

TEST_P(GuardDogActionsTest, KillShouldTriggerGuardDogActions) {
  auto die_function = [&]() -> void {
    const NiceMock<Configuration::MockWatchdog> config(DISABLE_MISS, DISABLE_MEGAMISS, 100, 0, 0,
                                                       getActionsConfig());
    setupFirstDog(config, Thread::ThreadId(10));
    time_system_->advanceTimeWait(std::chrono::milliseconds(101));
    guard_dog_->forceCheckForTest();
  };

  EXPECT_DEATH(die_function(), "ASSERT_GUARDDOG_ACTION");
}

TEST_P(GuardDogActionsTest, MultikillShouldTriggerGuardDogActions) {
  auto die_function = [&]() -> void {
    const NiceMock<Configuration::MockWatchdog> config(DISABLE_MISS, DISABLE_MEGAMISS, DISABLE_KILL,
                                                       100, 0, getActionsConfig());
    setupFirstDog(config, Thread::ThreadId(10));
    second_dog_ = guard_dog_->createWatchDog(Thread::ThreadId(11), "test_thread");
    guard_dog_->forceCheckForTest();
    time_system_->advanceTimeWait(std::chrono::milliseconds(101));
    guard_dog_->forceCheckForTest();
  };

  EXPECT_DEATH(die_function(), "ASSERT_GUARDDOG_ACTION");
}

} // namespace
} // namespace Server
} // namespace Envoy
