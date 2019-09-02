#include "common/common/thread.h"
#include "common/event/libevent.h"
#include "common/event/libevent_scheduler.h"
#include "common/event/timer_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "event2/event.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace Test {
namespace {

class SimulatedTimeSystemTest : public testing::Test {
protected:
  SimulatedTimeSystemTest()
      : scheduler_(time_system_.createScheduler(base_scheduler_)),
        start_monotonic_time_(time_system_.monotonicTime()),
        start_system_time_(time_system_.systemTime()) {}

  void addTask(int64_t delay_ms, char marker) {
    std::chrono::milliseconds delay(delay_ms);
    TimerPtr timer = scheduler_->createTimer(
        [this, marker, delay]() {
          output_.append(1, marker);
          EXPECT_GE(time_system_.monotonicTime(), start_monotonic_time_ + delay);
        },
        dispatcher_);
    timer->enableTimer(delay);
    timers_.push_back(std::move(timer));
  }

  void sleepMsAndLoop(int64_t delay_ms) {
    time_system_.sleep(std::chrono::milliseconds(delay_ms));
    base_scheduler_.run(Dispatcher::RunType::NonBlock);
  }

  void advanceSystemMsAndLoop(int64_t delay_ms) {
    time_system_.setSystemTime(time_system_.systemTime() + std::chrono::milliseconds(delay_ms));
    base_scheduler_.run(Dispatcher::RunType::NonBlock);
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  LibeventScheduler base_scheduler_;
  SimulatedTimeSystem time_system_;
  SchedulerPtr scheduler_;
  std::string output_;
  std::vector<TimerPtr> timers_;
  MonotonicTime start_monotonic_time_;
  SystemTime start_system_time_;
};

TEST_F(SimulatedTimeSystemTest, Sleep) {
  EXPECT_EQ(start_monotonic_time_, time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_, time_system_.systemTime());
  sleepMsAndLoop(5);
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(5), time_system_.systemTime());
}

TEST_F(SimulatedTimeSystemTest, WaitFor) {
  EXPECT_EQ(start_monotonic_time_, time_system_.monotonicTime());
  EXPECT_EQ(start_system_time_, time_system_.systemTime());

  // Run an event loop in the background to activate timers.
  std::atomic<bool> done(false);
  auto thread = Thread::threadFactoryForTest().createThread([this, &done]() {
    while (!done) {
      base_scheduler_.run(Dispatcher::RunType::Block);
    }
  });
  Thread::CondVar condvar;
  Thread::MutexBasicLockable mutex;
  TimerPtr timer = scheduler_->createTimer(
      [&condvar, &mutex, &done]() {
        Thread::LockGuard lock(mutex);
        done = true;
        condvar.notifyOne();
      },
      dispatcher_);
  timer->enableTimer(std::chrono::seconds(60));

  // Wait 50 simulated seconds of simulated time, which won't be enough to
  // activate the alarm. We'll get a fast automatic timeout in waitFor because
  // there are no pending timers.
  {
    Thread::LockGuard lock(mutex);
    EXPECT_EQ(Thread::CondVar::WaitStatus::Timeout,
              time_system_.waitFor(mutex, condvar, std::chrono::seconds(50)));
  }
  EXPECT_FALSE(done);
  EXPECT_EQ(MonotonicTime(std::chrono::seconds(50)), time_system_.monotonicTime());

  // Waiting another 20 simulated seconds will activate the alarm after 10,
  // and the event-loop thread will call the corresponding callback quickly.
  {
    Thread::LockGuard lock(mutex);
    EXPECT_EQ(Thread::CondVar::WaitStatus::NoTimeout,
              time_system_.waitFor(mutex, condvar, std::chrono::seconds(10)));
  }
  EXPECT_TRUE(done);
  EXPECT_EQ(MonotonicTime(std::chrono::seconds(60)), time_system_.monotonicTime());

  // Waiting a third time, with no pending timeouts, will just sleep out for
  // the max duration and return a timeout.
  {
    Thread::LockGuard lock(mutex);
    EXPECT_EQ(Thread::CondVar::WaitStatus::Timeout,
              time_system_.waitFor(mutex, condvar, std::chrono::seconds(20)));
  }
  EXPECT_EQ(MonotonicTime(std::chrono::seconds(80)), time_system_.monotonicTime());

  thread->join();
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
  sleepMsAndLoop(5);
  EXPECT_EQ("35", output_);
  sleepMsAndLoop(1);
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
  sleepMsAndLoop(5);
  EXPECT_EQ("3", output_);
  sleepMsAndLoop(1);
  EXPECT_EQ("36", output_);
}

TEST_F(SimulatedTimeSystemTest, IgnoreRedundantDisable) {
  addTask(5, '5');
  timers_[0]->disableTimer();
  timers_[0]->disableTimer();
  sleepMsAndLoop(5);
  EXPECT_EQ("", output_);
}

TEST_F(SimulatedTimeSystemTest, OverrideEnable) {
  addTask(5, '5');
  timers_[0]->enableTimer(std::chrono::milliseconds(6));
  sleepMsAndLoop(5);
  EXPECT_EQ("", output_); // Timer didn't wake up because we overrode to 6ms.
  sleepMsAndLoop(1);
  EXPECT_EQ("5", output_);
}

TEST_F(SimulatedTimeSystemTest, DeleteTime) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  timers_[0].reset();
  EXPECT_EQ("", output_);
  sleepMsAndLoop(5);
  EXPECT_EQ("3", output_);
  sleepMsAndLoop(1);
  EXPECT_EQ("36", output_);
}

// Regression test for issues documented in https://github.com/envoyproxy/envoy/pull/6956
TEST_F(SimulatedTimeSystemTest, DuplicateTimer) {
  // Set one alarm two times to test that pending does not get duplicated..
  std::chrono::milliseconds delay(0);
  TimerPtr zero_timer = scheduler_->createTimer([this]() { output_.append(1, '2'); }, dispatcher_);
  zero_timer->enableTimer(delay);
  zero_timer->enableTimer(delay);
  sleepMsAndLoop(1);
  EXPECT_EQ("2", output_);

  // Now set an alarm which requires 10ms of progress and make sure waitFor works.
  std::atomic<bool> done(false);
  auto thread = Thread::threadFactoryForTest().createThread([this, &done]() {
    while (!done) {
      base_scheduler_.run(Dispatcher::RunType::Block);
    }
  });
  Thread::CondVar condvar;
  Thread::MutexBasicLockable mutex;
  TimerPtr timer = scheduler_->createTimer(
      [&condvar, &mutex, &done]() {
        Thread::LockGuard lock(mutex);
        done = true;
        condvar.notifyOne();
      },
      dispatcher_);
  timer->enableTimer(std::chrono::seconds(10));

  {
    Thread::LockGuard lock(mutex);
    EXPECT_EQ(Thread::CondVar::WaitStatus::NoTimeout,
              time_system_.waitFor(mutex, condvar, std::chrono::seconds(10)));
  }
  EXPECT_TRUE(done);

  thread->join();
}

TEST_F(SimulatedTimeSystemTest, Enabled) {
  TimerPtr timer = scheduler_->createTimer({}, dispatcher_);
  timer->enableTimer(std::chrono::milliseconds(0));
  EXPECT_TRUE(timer->enabled());
}

} // namespace
} // namespace Test
} // namespace Event
} // namespace Envoy
