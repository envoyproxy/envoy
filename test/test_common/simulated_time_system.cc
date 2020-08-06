#include "test/test_common/simulated_time_system.h"

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/event/real_time_system.h"
#include "common/event/timer_impl.h"

namespace Envoy {
namespace Event {

namespace {
class UnlockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is unlocked upon construction.
   * The main motivation for setting up a class to manage this, rather than
   * simply { mutex.unlock(); operation(); mutex.lock(); } is that in method
   * Alarm::activateLockHeld(), the mutex is owned by the time-system, which
   * lives long enough. However the Alarm may be destructed while the lock is
   * dropped, so there can be a tsan error when re-taking time_system_.mutex_.
   *
   * It's also easy to make a temp mutex reference, however this confuses
   * clang's thread-annotation analysis, whereas this unlock-guard seems to work
   * with thread annotation.
   *
   * Another reason to use this Guard class is so that the mutex is re-taken
   * even if there is an exception thrown while the lock is dropped. That is
   * not likely to happen at this call-site as the functions being called don't
   * throw.
   *
   * @param lock the mutex.
   */
  explicit UnlockGuard(absl::Mutex& lock) : lock_(lock) { lock_.Unlock(); }

  /**
   * Destruction of the UnlockGuard re-locks the lock.
   */
  ~UnlockGuard() { lock_.Lock(); }

private:
  absl::Mutex& lock_;
};
} // namespace

// Our simulated alarm inherits from TimerImpl so that the same dispatching
// mechanism used in RealTimeSystem timers is employed for simulated alarms.
class SimulatedTimeSystemHelper::Alarm : public Timer {
public:
  Alarm(SimulatedTimeSystemHelper& time_system, CallbackScheduler& cb_scheduler, TimerCb cb)
      : cb_(cb_scheduler.createSchedulableCallback([this, cb] { runAlarm(cb); })),
        time_system_(time_system), armed_(false), pending_(false) {}

  ~Alarm() override;

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& duration,
                   const ScopeTrackedObject* scope) override {
    enableHRTimer(duration, scope);
  };
  void enableHRTimer(const std::chrono::microseconds& duration,
                     const ScopeTrackedObject* scope) override;
  bool enabled() override {
    absl::MutexLock lock(&time_system_.mutex_);
    return armed_ || cb_->enabled();
  }

  void disableTimerLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(time_system_.mutex_);

  /**
   * Activates the timer so it will be run the next time the libevent loop is run,
   * typically via Dispatcher::run().
   */
  void activateLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(time_system_.mutex_) {
    ASSERT(armed_);
    armed_ = false;
    if (pending_) {
      return;
    }
    pending_ = true;
    time_system_.incPendingLockHeld();

    // We don't want to activate the alarm under lock, as it will make a
    // libevent call, and libevent itself uses locks:
    // https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/event.c#L1917
    // See class comment for UnlockGuard for details on saving
    // time_system_.mutex_ prior to running libevent, which may delete this.
    UnlockGuard unlocker(time_system_.mutex_);
    cb_->scheduleCallbackCurrentIteration();
  }

  SimulatedTimeSystemHelper& timeSystem() { return time_system_; }

private:
  void runAlarm(TimerCb cb) {
    {
      absl::MutexLock lock(&time_system_.mutex_);
      pending_ = false;
    }
    // Capture time_system_ in a local in case the alarm gets deleted in the callback.
    SimulatedTimeSystemHelper& time_system = time_system_;
    cb();
    time_system.decPending();
  }

  SchedulableCallbackPtr cb_;
  SimulatedTimeSystemHelper& time_system_;
  bool armed_ ABSL_GUARDED_BY(time_system_.mutex_);
  bool pending_ ABSL_GUARDED_BY(time_system_.mutex_);
};

// Each timer is maintained and ordered by a common TimeSystem, but is
// associated with a scheduler. The scheduler creates the timers with a libevent
// context, so that the timer callbacks can be executed via Dispatcher::run() in
// the expected thread.
class SimulatedTimeSystemHelper::SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(SimulatedTimeSystemHelper& time_system, CallbackScheduler& cb_scheduler)
      : time_system_(time_system), cb_scheduler_(cb_scheduler) {}
  TimerPtr createTimer(const TimerCb& cb, Dispatcher& /*dispatcher*/) override {
    return std::make_unique<SimulatedTimeSystemHelper::Alarm>(time_system_, cb_scheduler_, cb);
  };

private:
  SimulatedTimeSystemHelper& time_system_;
  CallbackScheduler& cb_scheduler_;
};

SimulatedTimeSystemHelper::Alarm::Alarm::~Alarm() {
  if (armed_) {
    disableTimer();
  }
}

void SimulatedTimeSystemHelper::Alarm::Alarm::disableTimer() {
  cb_->cancel();
  absl::MutexLock lock(&time_system_.mutex_);
  disableTimerLockHeld();
}

void SimulatedTimeSystemHelper::Alarm::Alarm::disableTimerLockHeld() {
  if (armed_) {
    time_system_.removeAlarmLockHeld(*this);
    armed_ = false;
  }
  if (pending_) {
    pending_ = false;
    time_system_.decPendingLockHeld();
  }
}

void SimulatedTimeSystemHelper::Alarm::Alarm::enableHRTimer(
    const std::chrono::microseconds& duration, const ScopeTrackedObject* /*scope*/) {
  if (duration.count() != 0) {
    disableTimer();
  }
  absl::MutexLock lock(&time_system_.mutex_);
  if (pending_) {
    // Calling enableTimer on a timer that is already pending is a no-op. Timer will still fire
    // based on the original time it was scheduled.
    return;
  } else if (armed_) {
    disableTimerLockHeld();
  }

  armed_ = true;
  if (duration.count() == 0) {
    activateLockHeld();
  } else {
    time_system_.addAlarmLockHeld(*this, duration);
  }
}

// It would be very confusing if there were more than one simulated time system
// extant at once. Technically this might be something we want, but more likely
// it indicates some kind of plumbing error in test infrastructure. So track
// the instance count with a simple int. In the future if there's a good reason
// to have more than one around at a time, this variable can be deleted.
static int instance_count = 0;

// When we initialize our simulated time, we'll start the current time based on
// the real current time. But thereafter, real-time will not be used, and time
// will march forward only by calling.advanceTimeAsync().
SimulatedTimeSystemHelper::SimulatedTimeSystemHelper()
    : monotonic_time_(MonotonicTime(std::chrono::seconds(0))),
      system_time_(real_time_source_.systemTime()), pending_alarms_(0) {
  ++instance_count;
  ASSERT(instance_count <= 1);
}

SimulatedTimeSystemHelper::~SimulatedTimeSystemHelper() { --instance_count; }

bool SimulatedTimeSystemHelper::hasInstance() { return instance_count > 0; }

SystemTime SimulatedTimeSystemHelper::systemTime() {
  absl::MutexLock lock(&mutex_);
  return system_time_;
}

MonotonicTime SimulatedTimeSystemHelper::monotonicTime() {
  absl::MutexLock lock(&mutex_);
  return monotonic_time_;
}

void SimulatedTimeSystemHelper::advanceTimeAsync(const Duration& duration) {
  only_one_thread_.checkOneThread();
  absl::MutexLock lock(&mutex_);
  MonotonicTime monotonic_time =
      monotonic_time_ + std::chrono::duration_cast<MonotonicTime::duration>(duration);
  setMonotonicTimeLockHeld(monotonic_time);
}

void SimulatedTimeSystemHelper::advanceTimeWait(const Duration& duration) {
  only_one_thread_.checkOneThread();
  absl::MutexLock lock(&mutex_);
  MonotonicTime monotonic_time =
      monotonic_time_ + std::chrono::duration_cast<MonotonicTime::duration>(duration);
  setMonotonicTimeLockHeld(monotonic_time);
  waitForNoPendingLockHeld();
}

void SimulatedTimeSystemHelper::waitForNoPendingLockHeld() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  mutex_.Await(absl::Condition(
      +[](const uint32_t* pending_alarms) -> bool { return *pending_alarms == 0; },
      &pending_alarms_));
}

Thread::CondVar::WaitStatus SimulatedTimeSystemHelper::waitFor(Thread::MutexBasicLockable& mutex,
                                                               Thread::CondVar& condvar,
                                                               const Duration& duration) noexcept
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
  only_one_thread_.checkOneThread();

  // TODO(#10568): This real-time polling delay should not be necessary. Without
  // it, test/extensions/filters/http/cache:cache_filter_integration_test fails
  // about 40% of the time.
  const Duration real_time_poll_delay(
      std::min(std::chrono::duration_cast<Duration>(std::chrono::milliseconds(50)), duration));
  const MonotonicTime end_time = monotonicTime() + duration;

  bool timeout_not_reached = true;
  while (timeout_not_reached) {
    // First check to see if the condition is already satisfied without advancing sim time.
    if (condvar.waitFor(mutex, real_time_poll_delay) == Thread::CondVar::WaitStatus::NoTimeout) {
      return Thread::CondVar::WaitStatus::NoTimeout;
    }

    // This function runs with the caller-provided mutex held. We need to
    // hold this->mutex_ while accessing the timer-queue and blocking on
    // callbacks completing. To avoid potential deadlock we must drop
    // the caller's mutex before taking ours. We also must care to avoid
    // break/continue/return/throw during this non-RAII lock operation.
    mutex.unlock();
    {
      absl::MutexLock lock(&mutex_);
      if (monotonic_time_ < end_time) {
        MonotonicTime next_wakeup = end_time;
        if (!alarms_.empty()) {
          // If there's another alarm pending, sleep forward to it.
          const AlarmRegistration& alarm_registration = *alarms_.begin();
          next_wakeup = std::min(alarm_registration.time_, next_wakeup);
        }
        setMonotonicTimeLockHeld(next_wakeup);
        waitForNoPendingLockHeld();
      } else {
        // If we reached our end_time, break the loop and return timeout. We
        // don't break immediately as we have to drop mutex_ and re-take mutex,
        // and it's cleaner to have a linear flow to the end of the loop.
        timeout_not_reached = false;
      }
    }
    mutex.lock();
  }
  return Thread::CondVar::WaitStatus::Timeout;
}

void SimulatedTimeSystemHelper::alarmActivateLockHeld(Alarm& alarm) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // We disable thread-safety analysis as the compiler can't detect that
  // alarm_.timeSystem() == this, so we must be holding the right mutex.
  ASSERT(&(alarm.timeSystem()) == this);
  alarm.activateLockHeld();
}

void SimulatedTimeSystemHelper::addAlarmLockHeld(
    Alarm& alarm, const std::chrono::microseconds& duration) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT(&(alarm.timeSystem()) == this);
  ASSERT(alarms_.size() == alarm_registrations_map_.size());
  ASSERT(alarm_registrations_map_.find(&alarm) == alarm_registrations_map_.end());

  auto insert_result = alarms_.insert({monotonic_time_ + duration, random_source_.random(), alarm});
  ASSERT(insert_result.second);
  alarm_registrations_map_.emplace(&alarm, insert_result.first);

  // Sanity check that the parallel data structures used for alarm registration have the same number
  // of entries.
  ASSERT(alarms_.size() == alarm_registrations_map_.size());
}

void SimulatedTimeSystemHelper::removeAlarmLockHeld(Alarm& alarm) {
  ASSERT(alarms_.size() == alarm_registrations_map_.size());

  auto it = alarm_registrations_map_.find(&alarm);
  ASSERT(it != alarm_registrations_map_.end());
  alarms_.erase(it->second);
  alarm_registrations_map_.erase(it);

  // Sanity check that the parallel data structures used for alarm registration have the same number
  // of entries.
  ASSERT(alarms_.size() == alarm_registrations_map_.size());
}

SchedulerPtr SimulatedTimeSystemHelper::createScheduler(Scheduler& /*base_scheduler*/,
                                                        CallbackScheduler& cb_scheduler) {
  return std::make_unique<SimulatedScheduler>(*this, cb_scheduler);
}

void SimulatedTimeSystemHelper::setMonotonicTimeLockHeld(const MonotonicTime& monotonic_time) {
  // We don't have a MutexLock construct that allows temporarily
  // dropping the lock to run a callback. The main issue here is that we must
  // be careful not to be holding mutex_ when an exception can be thrown.
  // That can only happen here in alarm->activate(), which is run with the mutex
  // released.
  if (monotonic_time >= monotonic_time_) {
    // Alarms is a std::set ordered by wakeup time, so pulling off begin() each
    // iteration gives you wakeup order. Also note that alarms may be added
    // or removed during the call to activate() so it would not be correct to
    // range-iterate over the set.
    while (!alarms_.empty()) {
      const AlarmRegistration& alarm_registration = *alarms_.begin();
      MonotonicTime alarm_time = alarm_registration.time_;
      if (alarm_time > monotonic_time) {
        break;
      }
      ASSERT(alarm_time >= monotonic_time_);
      system_time_ +=
          std::chrono::duration_cast<SystemTime::duration>(alarm_time - monotonic_time_);
      monotonic_time_ = alarm_time;
      Alarm& alarm = alarm_registration.alarm_;
      removeAlarmLockHeld(alarm);
      alarmActivateLockHeld(alarm);
    }
    system_time_ +=
        std::chrono::duration_cast<SystemTime::duration>(monotonic_time - monotonic_time_);
    monotonic_time_ = monotonic_time;
  }
}

void SimulatedTimeSystemHelper::setSystemTime(const SystemTime& system_time) {
  absl::MutexLock lock(&mutex_);
  if (system_time > system_time_) {
    MonotonicTime monotonic_time =
        monotonic_time_ +
        std::chrono::duration_cast<MonotonicTime::duration>(system_time - system_time_);
    setMonotonicTimeLockHeld(monotonic_time);
  } else {
    system_time_ = system_time;
  }
}

} // namespace Event
} // namespace Envoy
