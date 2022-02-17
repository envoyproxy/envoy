#pragma once

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

#include "test/test_common/global.h"
#include "test/test_common/only_one_thread.h"

namespace Envoy {
namespace Event {

// Adds sleep() and waitFor() interfaces to Event::TimeSystem.
class TestTimeSystem : public Event::TimeSystem {
public:
  ~TestTimeSystem() override = default;

  /**
   * This class will use the real monotonic time regardless of the time system in use (real
   * or simulated). This should only be used when time is needed for real timeouts that govern
   * networking, etc. It should never be used for time that only advances explicitly for alarms.
   */
  class RealTimeBound {
  public:
    template <class D>
    RealTimeBound(const D& duration)
        : end_time_(std::chrono::steady_clock::now() + duration) // NO_CHECK_FORMAT(real_time)
    {}

    std::chrono::milliseconds timeLeft() {
      const auto current_time = std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
      if (current_time > end_time_) {
        return std::chrono::milliseconds(0);
      }
      return std::chrono::duration_cast<std::chrono::milliseconds>(end_time_ - current_time);
    }

    bool withinBound() {
      return std::chrono::steady_clock::now() < end_time_; // NO_CHECK_FORMAT(real_time)
    }

  private:
    const MonotonicTime end_time_;
  };

  /**
   * Advances time forward by the specified duration, running any timers
   * scheduled to fire, and blocking until the timer callbacks are complete.
   * See also advanceTimeAndRun(), which provides the option to run a specific
   * dispatcher or scheduler after advancing the time.
   *
   * This function should be used in multi-threaded tests, where other
   * threads are running dispatcher loops. Integration tests should usually
   * use this variant.
   *
   * @param duration The amount of time to advance.
   */
  virtual void advanceTimeWaitImpl(const Duration& duration) PURE;
  template <class D> void advanceTimeWait(const D& duration = false) {
    advanceTimeWaitImpl(std::chrono::duration_cast<Duration>(duration));
  }

  /**
   * Advances time forward by the specified duration. Timers on event loops outside the current
   * thread may trigger, but unlike advanceTimeWait(), this method does not block waiting for them
   * to complete. This method also takes in a parameter the dispatcher or scheduler for the current
   * thread, which will be run in the requested mode after advancing the time forward.
   *
   * This function should be used in single-threaded tests that want to advance time and then run
   * the test thread event loop. Unit tests will often use this variant.
   *
   * @param duration The amount of time to advance.
   * @param dispatcher_or_scheduler The event loop to run after advancing time forward.
   * @param mode The mode to use when running the event loop.
   */
  template <class D, class DispatcherOrScheduler>
  void advanceTimeAndRun(const D& duration, DispatcherOrScheduler& dispatcher_or_scheduler,
                         Dispatcher::RunType mode) {
    advanceTimeAsyncImpl(std::chrono::duration_cast<Duration>(duration));
    dispatcher_or_scheduler.run(mode);
  }

  /**
   * Helper function used by the implementation of advanceTimeAndRun which just advances time
   * forward by the specified amount.
   *
   * @param duration The amount of time to advance.
   */
  virtual void advanceTimeAsyncImpl(const Duration& duration) PURE;

  /**
   * Waits for the specified duration to expire, or for the condition to be satisfied, whichever
   * comes first.
   *
   * NOTE: This function takes a duration parameter which is the timeout of the wait. This is *real*
   *       time in all time systems. This is to avoid test hangs and provide a useful error message.
   *       When using simulated time this does not advance monotonic time. Thus, to simulated time
   *       tests all network behavior will appear instantaneous. If time needs to advance to fire
   *       alarms advanceTimeWait() or advanceTimeAsync() should be used.
   *
   * @param mutex A mutex which must be held before calling this function.
   * @param condition The condition to wait on.
   * @param duration The maximum amount of time to wait.
   * @return Thread::CondVar::WaitStatus whether the condition timed out or not.
   */
  template <class D>
  bool waitFor(absl::Mutex& mutex, const absl::Condition& condition, const D& duration) noexcept
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    only_one_thread_.checkOneThread();
    return mutex.AwaitWithTimeout(condition,
                                  absl::FromChrono(std::chrono::duration_cast<Duration>(duration)));
  }

  /**
   * This function will perform a real sleep in all time systems (real or simulated). This function
   * should NOT be used without a good reason. It is either for supporting old code that needs to
   * be converted to an event based approach, simulated time, or some other solution. Be ready
   * to explain why you are using this in code review.
   */
  template <class D> void realSleepDoNotUseWithoutScrutiny(const D& duration) {
    std::this_thread::sleep_for(duration); // NO_CHECK_FORMAT(real_time)
  }

protected:
  Thread::OnlyOneThread only_one_thread_;
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
  void advanceTimeAsyncImpl(const Duration& duration) override {
    timeSystem().advanceTimeAsyncImpl(duration);
  }
  void advanceTimeWaitImpl(const Duration& duration) override {
    timeSystem().advanceTimeWaitImpl(duration);
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
