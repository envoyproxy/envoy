#pragma once

#include "envoy/event/timer.h"

#include "common/common/assert.h"
#include "common/common/thread.h"

#include "test/test_common/global.h"

namespace Envoy {
namespace Event {

// Adds sleep() and waitFor() interfaces to Event::TimeSystem.
class TestTimeSystem : public Event::TimeSystem {
public:
  ~TestTimeSystem() override = default;

  /**
   * Advances time forward by the specified duration, running any timers
   * scheduled to fire, and blocking until the timer callbacks are complete.
   * See also advanceTimeAsync(), which does not block.
   *
   * This function should be used in multi-threaded tests, where other
   * threads are running dispatcher loops. Integration tests should usually
   * use this variant.
   *
   * @param duration The amount of time to sleep.
   * @param fixfix
   */
  virtual void advanceTimeWaitImpl(const Duration& duration, bool always_sleep) PURE;
  template <class D> void advanceTimeWait(const D& duration, bool always_sleep = false) {
    advanceTimeWaitImpl(std::chrono::duration_cast<Duration>(duration), always_sleep);
  }

  /**
   * Advances time forward by the specified duration. Timers may be triggered on
   * their threads, but unlike advanceTimeWait(), this method does not block
   * waiting for them to complete.
   *
   * This function should be used in single-threaded tests, in scenarios where
   * after time is advanced, the main test thread will run a dispatcher
   * loop. Unit tests will often use this variant.
   *
   * @param duration The amount of time to sleep.
   * @param fixfix
   */
  virtual void advanceTimeAsyncImpl(const Duration& duration, bool always_sleep) PURE;
  template <class D> void advanceTimeAsync(const D& duration, bool always_sleep = false) {
    advanceTimeAsyncImpl(std::chrono::duration_cast<Duration>(duration), always_sleep);
  }

  /**
   * Waits for the specified duration to expire, or for a condvar to
   * be notified, whichever comes first.
   *
   * @param mutex A mutex which must be held before calling this function.
   * @param condvar The condition to wait on.
   * @param duration The maximum amount of time to wait.
   * @param fixfix
   * @return Thread::CondVar::WaitStatus whether the condition timed out or not.
   */
  virtual bool waitForImpl(absl::Mutex& mutex, const absl::Condition& condition,
                           const Duration& duration, bool always_sleep) noexcept
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) PURE;

  template <class D>
  bool waitFor(absl::Mutex& mutex, const absl::Condition& condition, const D& duration,
               bool always_sleep = false) noexcept ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    return waitForImpl(mutex, condition, std::chrono::duration_cast<Duration>(duration),
                       always_sleep);
  }
};

// There should only be one instance of any time-system resident in a test
// process at once. This helper class is used with Test::Global to help enforce
// that with an ASSERT. Each time-system derivation should have a helper
// implementation which is referenced from a delegate (see
// DelegatingTestTimeSystemBase). In each delegate, a SingletonTimeSystemHelper
// should be instantiated via Test::Global<SingletonTimeSystemHelper>. Only one
// instance of SingletonTimeSystemHelper per process, at a time. When all
// references to the delegates are destructed, the singleton will be destroyed
// as well, so each test-method will get a fresh start.
class SingletonTimeSystemHelper {
public:
  SingletonTimeSystemHelper() : time_system_(nullptr) {}

  using MakeTimeSystemFn = std::function<std::unique_ptr<TestTimeSystem>()>;

  /**
   * Returns a singleton time-system, creating a default one of there's not
   * one already. This method is thread-safe.
   *
   * @return the time system.
   */
  TestTimeSystem& timeSystem(const MakeTimeSystemFn& make_time_system);

private:
  std::unique_ptr<TestTimeSystem> time_system_ ABSL_GUARDED_BY(mutex_);
  Thread::MutexBasicLockable mutex_;
};

// Implements the TestTimeSystem interface, delegating implementation of all
// methods to a TestTimeSystem reference supplied by a timeSystem() method in a
// subclass.
template <class TimeSystemVariant> class DelegatingTestTimeSystemBase : public TestTimeSystem {
public:
  void advanceTimeAsyncImpl(const Duration& duration, bool always_sleep) override {
    timeSystem().advanceTimeAsyncImpl(duration, always_sleep);
  }
  void advanceTimeWaitImpl(const Duration& duration, bool always_sleep) override {
    timeSystem().advanceTimeWaitImpl(duration, always_sleep);
  }

  bool waitForImpl(absl::Mutex& mutex, const absl::Condition& condition, const Duration& duration,
                   bool always_sleep) noexcept ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) override {
    return timeSystem().waitForImpl(mutex, condition, duration, always_sleep);
  }

  SchedulerPtr createScheduler(Scheduler& base_scheduler,
                               CallbackScheduler& cb_scheduler) override {
    return timeSystem().createScheduler(base_scheduler, cb_scheduler);
  }
  SystemTime systemTime() override { return timeSystem().systemTime(); }
  MonotonicTime monotonicTime() override { return timeSystem().monotonicTime(); }

  TimeSystemVariant& operator*() { return timeSystem(); }

  virtual TimeSystemVariant& timeSystem() PURE;
};

// Wraps a concrete time-system in a delegate that ensures there is only one
// time-system of any variant resident in a process at a time. Attempts to
// instantiate multiple instances of the same type of time-system will simply
// reference the same shared delegate, which will be deleted when the last one
// goes out of scope. Attempts to instantiate different types of type-systems
// will result in a RELEASE_ASSERT. See the testcases in
// test_time_system_test.cc to understand the allowable sequences.
template <class TimeSystemVariant>
class DelegatingTestTimeSystem : public DelegatingTestTimeSystemBase<TimeSystemVariant> {
public:
  DelegatingTestTimeSystem() : time_system_(initTimeSystem()) {}

  TimeSystemVariant& timeSystem() override { return time_system_; }

private:
  TimeSystemVariant& initTimeSystem() {
    auto make_time_system = []() -> std::unique_ptr<TestTimeSystem> {
      return std::make_unique<TimeSystemVariant>();
    };
    auto time_system = dynamic_cast<TimeSystemVariant*>(&singleton_->timeSystem(make_time_system));
    RELEASE_ASSERT(time_system,
                   "Two different types of time-systems allocated. If deriving from "
                   "Event::TestUsingSimulatedTime make sure it is the first base class.");
    return *time_system;
  }

  Test::Global<SingletonTimeSystemHelper> singleton_;
  TimeSystemVariant& time_system_;
};

} // namespace Event
} // namespace Envoy
