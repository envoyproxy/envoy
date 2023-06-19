#include "source/common/common/thread.h"
#include "source/common/event/libevent.h"
#include "source/common/event/libevent_scheduler.h"
#include "source/common/event/timer_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "event2/event.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace Test {
namespace {

class SimulatedTimeSystemTest : public testing::Test {
protected:
  SimulatedTimeSystemTest()
      : scheduler_(time_system_.createScheduler(base_scheduler_, base_scheduler_)),
        start_monotonic_time_(time_system_.monotonicTime()),
        start_system_time_(time_system_.systemTime()) {}

  void trackPrepareCalls() {
    base_scheduler_.registerOnPrepareCallback([this]() { output_.append(1, 'p'); });
  }

  void addTask(int64_t delay_ms, char marker, bool expect_monotonic = true) {
    addCustomTask(
        delay_ms, marker, []() {}, expect_monotonic);
  }

  void addCustomTask(int64_t delay_ms, char marker, std::function<void()> cb,
                     bool expect_monotonic = true) {
    std::chrono::milliseconds delay(delay_ms);
    TimerPtr timer = scheduler_->createTimer(
        [this, marker, delay, cb, expect_monotonic]() {
          output_.append(1, marker);
          if (expect_monotonic) {
            EXPECT_GE(time_system_.monotonicTime(), start_monotonic_time_ + delay);
          }
          cb();
        },
        dispatcher_);
    timer->enableTimer(delay);
    timers_.push_back(std::move(timer));
  }

  void advanceMsAndLoop(int64_t delay_ms) {
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(delay_ms), base_scheduler_,
                                   Dispatcher::RunType::NonBlock);
  }

  void advanceSystemMsAndLoop(int64_t delay_ms) {
    time_system_.setSystemTime(time_system_.systemTime() + std::chrono::milliseconds(delay_ms));
    base_scheduler_.run(Dispatcher::RunType::NonBlock);
  }

  Event::MockDispatcher dispatcher_;
  LibeventScheduler base_scheduler_;
  SimulatedTimeSystem time_system_;
  SchedulerPtr scheduler_;
  std::string output_;
  std::vector<TimerPtr> timers_;
  MonotonicTime start_monotonic_time_;
  SystemTime start_system_time_;
};

TEST_F(SimulatedTimeSystemTest, AdvanceTimeAsync) {
  EXPECT_EQ(start_monotonic_time_, time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_, time_system_.systemTime());
  advanceMsAndLoop(5);
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(5), time_system_.systemTime());
}

TEST_F(SimulatedTimeSystemTest, TimerTotalOrdering) {
  trackPrepareCalls();

  addTask(0, '0');
  addTask(1, '1');
  addTask(2, '2');
  EXPECT_EQ(3, timers_.size());

  advanceMsAndLoop(5);

  // Verify order.
  EXPECT_EQ("p012", output_);
}

TEST_F(SimulatedTimeSystemTest, TimerPartialOrdering) {
  trackPrepareCalls();

  std::set<std::string> outputs;
  for (int i = 0; i < 100; ++i) {
    addTask(0, '0');
    addTask(1, '1');
    addTask(1, '2');
    addTask(3, '3');
    EXPECT_EQ(4, timers_.size());

    advanceMsAndLoop(5);

    outputs.insert(output_);

    // Cleanup before the next iteration.
    output_.clear();
    timers_.clear();
  }

  // Execution order of timers 1 and 2 is non-deterministic because the two timers were scheduled
  // for the same time. Verify that both orderings were observed.
  EXPECT_THAT(outputs, testing::ElementsAre("p0123", "p0213"));
}

TEST_F(SimulatedTimeSystemTest, TimerPartialOrdering2) {
  trackPrepareCalls();

  std::set<std::string> outputs;
  for (int i = 0; i < 100; ++i) {
    addTask(0, '0');
    addTask(15, '1');
    advanceMsAndLoop(10);

    // Timer 1 has 5ms remaining, so timer 2 ends up scheduled at the same monotonic time as 1.
    addTask(5, '2');
    addTask(6, '3');
    advanceMsAndLoop(10);

    outputs.insert(output_);

    // Cleanup before the next iteration.
    output_.clear();
    timers_.clear();
  }

  // Execution order of timers 1 and 2 is non-deterministic because the two timers were scheduled
  // for the same time. Verify that both orderings were observed.
  EXPECT_THAT(outputs, testing::ElementsAre("p0p123", "p0p213"));
}

// Timers that are scheduled to execute and but are disabled first do not trigger.
TEST_F(SimulatedTimeSystemTest, TimerOrderAndDisableTimer) {
  trackPrepareCalls();

  // Create 3 timers. The first timer should disable the second, so it doesn't trigger.
  addCustomTask(0, '0', [this]() { timers_[1]->disableTimer(); });
  addTask(1, '1');
  addTask(2, '2');
  EXPECT_EQ(3, timers_.size());

  // Expect timers to execute in order since the timers are scheduled at have different times and
  // that timer 1 does not execute because it was disabled as part of 0's execution.
  advanceMsAndLoop(5);
  // Verify that timer 1 was skipped.
  EXPECT_EQ("p02", output_);
}

// Capture behavior of timers which are rescheduled without being disabled first.
TEST_F(SimulatedTimeSystemTest, TimerOrderAndRescheduleTimer) {
  trackPrepareCalls();

  // Reschedule timers 1, 2 and 4 without disabling first.
  addCustomTask(0, '0', [this]() {
    timers_[1]->enableTimer(std::chrono::milliseconds(0));
    timers_[2]->enableTimer(std::chrono::milliseconds(100));
    timers_[4]->enableTimer(std::chrono::milliseconds(0));
  });
  addTask(1, '1');
  addTask(2, '2');
  addTask(3, '3');
  addTask(10000, '4', false);
  EXPECT_EQ(5, timers_.size());

  // Rescheduling timers that are already scheduled to run in the current event loop iteration has
  // no effect if the time delta is 0. Expect timers 0, 1 and 3 to execute in the original order.
  // Timer 4 runs as part of the first wakeup since its new schedule time has a delta of 0. Timer 2
  // is delayed since it is rescheduled with a non-zero delta.
  advanceMsAndLoop(5);
  if constexpr (Event::PlatformDefaultTriggerType == FileTriggerType::Level) {
    // Force it to run again to pick up next iteration callbacks.
    // The event loop runs for a single iteration in NonBlock mode on Windows as a hack to work
    // around LEVEL trigger fd registrations constantly firing events and preventing the NonBlock
    // event loop from ever reaching the no-fd event and no-expired timers termination condition.
    // It is not possible to get consistent event loop behavior since the time system does not
    // override the base scheduler's run behavior, and libevent does not provide a mode where it
    // runs at most N iterations before breaking out of the loop for us to prefer over the single
    // iteration mode used on Windows.
    advanceMsAndLoop(0);
  }
  EXPECT_EQ("p013p4", output_);

  advanceMsAndLoop(100);
  EXPECT_EQ("p013p4p2", output_);
}

// Disable and re-enable timers that is already pending execution and verify that execution is
// delayed.
TEST_F(SimulatedTimeSystemTest, TimerOrderDisableAndRescheduleTimer) {
  trackPrepareCalls();

  // Disable and reschedule timers 1, 2 and 4 when timer 0 triggers.
  addCustomTask(0, '0', [this]() {
    timers_[1]->disableTimer();
    timers_[1]->enableTimer(std::chrono::milliseconds(0));
    timers_[2]->disableTimer();
    timers_[2]->enableTimer(std::chrono::milliseconds(100));
    timers_[4]->disableTimer();
    timers_[4]->enableTimer(std::chrono::milliseconds(0));
  });
  addTask(1, '1');
  addTask(2, '2');
  addTask(3, '3');
  addTask(10000, '4', false);
  EXPECT_EQ(5, timers_.size());

  // timer 0 is expected to run first and reschedule timers 1 and 2. Timer 3 should fire before
  // timer 1 since timer 3's registration is unaffected. timer 1 runs in the same iteration
  // because it is scheduled with zero delay. Timer 2 executes in a later iteration because it is
  // re-enabled with a non-zero timeout.
  advanceMsAndLoop(5);
  if constexpr (Event::PlatformDefaultTriggerType == FileTriggerType::Level) {
    // The event loop runs for a single iteration in NonBlock mode on Windows. Force it to run
    // again to pick up next iteration callbacks.
    advanceMsAndLoop(0);
  }
  EXPECT_THAT(output_, testing::AnyOf("p03p14", "p03p41"));

  advanceMsAndLoop(100);
  EXPECT_THAT(output_, testing::AnyOf("p03p14p2", "p03p41p2"));
}

TEST_F(SimulatedTimeSystemTest, AdvanceTimeWait) {
  EXPECT_EQ(start_monotonic_time_, time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_, time_system_.systemTime());

  addTask(4, 'Z');
  addTask(2, 'X');
  addTask(3, 'Y');
  addTask(6, 'A'); // This timer will never be run, so "A" will not be appended.
  std::atomic<bool> done(false);
  auto thread = Thread::threadFactoryForTest().createThread([this, &done]() {
    while (!done) {
      base_scheduler_.run(Dispatcher::RunType::Block);
    }
  });
  time_system_.advanceTimeWait(std::chrono::milliseconds(5));
  EXPECT_EQ("XYZ", output_);
  done = true;
  thread->join();
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(5), time_system_.systemTime());
}

TEST_F(SimulatedTimeSystemTest, WaitFor) {
  EXPECT_EQ(start_monotonic_time_, time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_, time_system_.systemTime());

  // Run an event loop in the background to activate timers.
  absl::Mutex mutex;
  bool done(false);
  auto thread = Thread::threadFactoryForTest().createThread([this, &mutex, &done]() {
    for (;;) {
      {
        absl::MutexLock lock(&mutex);
        if (done) {
          return;
        }
      }

      base_scheduler_.run(Dispatcher::RunType::Block);
    }
  });

  TimerPtr timer = scheduler_->createTimer(
      [&mutex, &done]() {
        absl::MutexLock lock(&mutex);
        done = true;
      },
      dispatcher_);
  timer->enableTimer(std::chrono::seconds(60));

  // Wait 1ms of real time. waitFor() does not advance simulated time, so this is just going to
  // verify that we return quickly and nothing has fired.
  {
    absl::MutexLock lock(&mutex);
    EXPECT_FALSE(time_system_.waitFor(mutex, absl::Condition(&done), std::chrono::milliseconds(1)));
  }
  EXPECT_FALSE(done);
  EXPECT_EQ(MonotonicTime(std::chrono::seconds(0)), time_system_.monotonicTime());

  // Fire the timeout by advancing time and then verify that waitFor() returns without any timeout.
  time_system_.advanceTimeWait(std::chrono::seconds(60));
  {
    absl::MutexLock lock(&mutex);
    EXPECT_TRUE(time_system_.waitFor(mutex, absl::Condition(&done), std::chrono::seconds(0)));
  }
  EXPECT_TRUE(done);
  EXPECT_EQ(MonotonicTime(std::chrono::seconds(60)), time_system_.monotonicTime());
  thread->join();

  // Waiting a third time, with no pending timeouts, will just sleep out for
  // the max duration and return a timeout.
  done = false;
  {
    absl::MutexLock lock(&mutex);
    EXPECT_FALSE(time_system_.waitFor(mutex, absl::Condition(&done), std::chrono::seconds(0)));
  }
  EXPECT_FALSE(done);
  EXPECT_EQ(MonotonicTime(std::chrono::seconds(60)), time_system_.monotonicTime());
}

TEST_F(SimulatedTimeSystemTest, Monotonic) {
  // Setting time forward works.
  time_system_.setMonotonicTime(start_monotonic_time_ + std::chrono::milliseconds(5));
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), time_system_.monotonicTime());

  // But going backward does not.
  time_system_.setMonotonicTime(start_monotonic_time_ + std::chrono::milliseconds(3));
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), time_system_.monotonicTime());
}

TEST_F(SimulatedTimeSystemTest, System) {
  // Setting time forward works.
  time_system_.setSystemTime(start_system_time_ + std::chrono::milliseconds(5));
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(5), time_system_.systemTime());

  // And going backward works too.
  time_system_.setSystemTime(start_system_time_ + std::chrono::milliseconds(3));
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(3), time_system_.systemTime());
}

TEST_F(SimulatedTimeSystemTest, Ordering) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  EXPECT_EQ("", output_);
  advanceMsAndLoop(5);
  EXPECT_EQ("35", output_);
  advanceMsAndLoop(1);
  EXPECT_EQ("356", output_);
}

TEST_F(SimulatedTimeSystemTest, SystemTimeOrdering) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  EXPECT_EQ("", output_);
  advanceSystemMsAndLoop(5);
  EXPECT_EQ("35", output_);
  advanceSystemMsAndLoop(1);
  EXPECT_EQ("356", output_);
  time_system_.setSystemTime(start_system_time_ + std::chrono::milliseconds(1));
  time_system_.setSystemTime(start_system_time_ + std::chrono::milliseconds(100));
  EXPECT_EQ("356", output_); // callbacks don't get replayed.
}

TEST_F(SimulatedTimeSystemTest, DisableTimer) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  timers_[0]->disableTimer();
  EXPECT_EQ("", output_);
  advanceMsAndLoop(5);
  EXPECT_EQ("3", output_);
  advanceMsAndLoop(1);
  EXPECT_EQ("36", output_);
}

TEST_F(SimulatedTimeSystemTest, IgnoreRedundantDisable) {
  addTask(5, '5');
  timers_[0]->disableTimer();
  timers_[0]->disableTimer();
  advanceMsAndLoop(5);
  EXPECT_EQ("", output_);
}

TEST_F(SimulatedTimeSystemTest, OverrideEnable) {
  addTask(5, '5');
  timers_[0]->enableTimer(std::chrono::milliseconds(6));
  advanceMsAndLoop(5);
  EXPECT_EQ("", output_); // Timer didn't wake up because we overrode to 6ms.
  advanceMsAndLoop(1);
  EXPECT_EQ("5", output_);
}

TEST_F(SimulatedTimeSystemTest, DeleteTime) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  timers_[0].reset();
  EXPECT_EQ("", output_);
  advanceMsAndLoop(5);
  EXPECT_EQ("3", output_);
  advanceMsAndLoop(1);
  EXPECT_EQ("36", output_);
}

// Regression test for issues documented in https://github.com/envoyproxy/envoy/pull/6956
TEST_F(SimulatedTimeSystemTest, DuplicateTimer) {
  // Set one alarm two times to test that pending does not get duplicated..
  std::chrono::milliseconds delay(0);
  TimerPtr zero_timer = scheduler_->createTimer([this]() { output_.append(1, '2'); }, dispatcher_);
  zero_timer->enableTimer(delay);
  zero_timer->enableTimer(delay);
  advanceMsAndLoop(1);
  EXPECT_EQ("2", output_);
}

// Regression test for issues documented in https://github.com/envoyproxy/envoy/pull/6956
TEST_F(SimulatedTimeSystemTest, DuplicateTimer2) {
  // Now set an alarm which requires 10s of progress and make sure advanceTimeWait and waitFor
  // works.
  absl::Mutex mutex;
  bool done(false);
  auto thread = Thread::threadFactoryForTest().createThread([this, &mutex, &done]() {
    for (;;) {
      {
        absl::MutexLock lock(&mutex);
        if (done) {
          return;
        }
      }

      base_scheduler_.run(Dispatcher::RunType::Block);
    }
  });

  TimerPtr timer = scheduler_->createTimer(
      [&mutex, &done]() {
        absl::MutexLock lock(&mutex);
        done = true;
      },
      dispatcher_);
  timer->enableTimer(std::chrono::seconds(10));

  {
    absl::MutexLock lock(&mutex);
    EXPECT_FALSE(time_system_.waitFor(mutex, absl::Condition(&done), std::chrono::seconds(0)));
  }
  EXPECT_FALSE(done);

  time_system_.advanceTimeWait(std::chrono::seconds(10));
  {
    absl::MutexLock lock(&mutex);
    EXPECT_TRUE(time_system_.waitFor(mutex, absl::Condition(&done), std::chrono::seconds(0)));
  }
  EXPECT_TRUE(done);

  thread->join();
}

TEST_F(SimulatedTimeSystemTest, Enabled) {
  TimerPtr timer = scheduler_->createTimer({}, dispatcher_);
  timer->enableTimer(std::chrono::milliseconds(0));
  EXPECT_TRUE(timer->enabled());
}

TEST_F(SimulatedTimeSystemTest, DeleteTimerFromThread) {
  TimerPtr timer = scheduler_->createTimer([]() {}, dispatcher_);
  timer->enableTimer(std::chrono::milliseconds(0));
  auto thread = Thread::threadFactoryForTest().createThread([&timer]() { timer.reset(); });
  advanceMsAndLoop(1);
  thread->join();
}

TEST_F(SimulatedTimeSystemTest, DeleteTimerFromThread2) {
  TimerPtr timer = scheduler_->createTimer([]() {}, dispatcher_);
  timer->enableTimer(std::chrono::milliseconds(1));
  auto thread = Thread::threadFactoryForTest().createThread([&timer]() { timer.reset(); });
  advanceMsAndLoop(1);
  thread->join();
}

} // namespace
} // namespace Test
} // namespace Event
} // namespace Envoy
