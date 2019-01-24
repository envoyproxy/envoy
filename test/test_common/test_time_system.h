#pragma once

#include "envoy/event/timer.h"

#include "common/common/assert.h"
#include "common/common/thread.h"

#include "test/test_common/global.h"

namespace Envoy {
namespace Event {

class TestTimeSystem;

// Ensures that only one type of time-system is instantiated at a time.
class SingletonTimeSystemHelper {
public:
  explicit SingletonTimeSystemHelper() : time_system_(nullptr) {}

  void set(TestTimeSystem* time_system) {
    if (time_system_ == nullptr) {
      time_system_.reset(time_system);
    } else {
      ASSERT(time_system_.get() == time_system);
    }
  }

  TestTimeSystem* timeSystem() { return time_system_.get(); }

private:
  std::unique_ptr<TestTimeSystem> time_system_;
};

// Adds sleep() and waitFor() interfaces to Event::TimeSystem.
class TestTimeSystem : public Event::TimeSystem {
public:
  virtual ~TestTimeSystem() = default;

  /**
   * Advances time forward by the specified duration, running any timers
   * along the way that have been scheduled to fire.
   *
   * @param duration The amount of time to sleep.
   */
  virtual void sleep(const Duration& duration) PURE;
  template <class D> void sleep(const D& duration) {
    sleep(std::chrono::duration_cast<Duration>(duration));
  }

  /**
   * Waits for the specified duration to expire, or for a condvar to
   * be notified, whichever comes first.
   *
   * @param mutex A mutex which must be held before calling this function.
   * @param condvar The condition to wait on.
   * @param duration The maximum amount of time to wait.
   * @return Thread::CondVar::WaitStatus whether the condition timed out or not.
   */
  virtual Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) PURE;

  template <class D>
  Thread::CondVar::WaitStatus waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
                                      const D& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    return waitFor(mutex, condvar, std::chrono::duration_cast<Duration>(duration));
  }
};

template<class TimeSystemVariant>
class DelegatingTestTimeSystem : public TestTimeSystem {
 public:
  DelegatingTestTimeSystem() {
    TestTimeSystem* time_system = singleton_->timeSystem();
    if (time_system == nullptr) {
      time_system_ = new TimeSystemVariant;
      singleton_->set(time_system_);
    } else {
      time_system_ = dynamic_cast<TimeSystemVariant*>(time_system);
      ASSERT(time_system_);
    }
  }

  void sleep(const Duration& duration) override { time_system_->sleep(duration); }

  Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) override {
    return time_system_->waitFor(mutex, condvar, duration);
  }

  SchedulerPtr createScheduler(Libevent::BasePtr& base_ptr) override {
    return time_system_->createScheduler(base_ptr);
  }
  SystemTime systemTime() override { return time_system_->systemTime(); }
  MonotonicTime monotonicTime() override { return time_system_->monotonicTime(); }

  TimeSystemVariant& operator*() { return *time_system_; }

 protected:
  TimeSystemVariant* time_system_;

 private:
  Test::Global<SingletonTimeSystemHelper> singleton_;
};

} // namespace Event
} // namespace Envoy
