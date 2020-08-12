#include "test/test_common/simulated_time_system.h"

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/event/real_time_system.h"
#include "common/event/timer_impl.h"
#include "common/runtime/runtime_features.h"

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

// Each timer is maintained and ordered by a common TimeSystem, but is
// associated with a scheduler. The scheduler creates the timers with a libevent
// context, so that the timer callbacks can be executed via Dispatcher::run() in
// the expected thread.
class SimulatedTimeSystemHelper::SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(SimulatedTimeSystemHelper& time_system, CallbackScheduler& cb_scheduler)
      : time_system_(time_system), cb_scheduler_(cb_scheduler),
        update_time_cb_(
            cb_scheduler.createSchedulableCallback([this] { updateTimeAndTriggerAlarms(); })),
        run_alarms_cb_(cb_scheduler.createSchedulableCallback([this] { runReadyAlarms(); })) {
    time_system_.addScheduler(this);
  }
  ~SimulatedScheduler() override { time_system_.removeScheduler(this); }

  absl::optional<MonotonicTime> minAlarmRegistrationTime();

  TimerPtr createTimer(const TimerCb& cb, Dispatcher& /*dispatcher*/) override;

  bool isEnabled(Alarm& alarm);
  void enableAlarm(Alarm& alarm, const std::chrono::microseconds& duration);
  void disableAlarm(Alarm& alarm) {
    absl::MutexLock lock(&mutex_);
    disableAlarmLockHeld(alarm);
  }

  void waitUntilIdle() {
    absl::MutexLock lock(&mutex_);
    mutex_.Await(absl::Condition(&not_running_cbs_));
  }

  void updateTime(MonotonicTime monotonic_time, SystemTime system_time) {
    bool inc_pending = false;
    {
      absl::MutexLock lock(&mutex_);
      next_monotonic_time_ = monotonic_time;
      next_system_time_ = system_time;
      if (!pending_dec_ && (!registered_alarms_.empty() || !triggered_alarms_.empty())) {
        // HACK: selectively increment only on dispatchers that have active alarms to allow the
        // pending alarms mechanism to be used to detect when alarms had their times updated, while
        // also avoiding waiting for decrements from dispatchers that are either not active or are
        // blocked on epoll for a long time because of lack of other events that would wake them up.
        // There is a known, but not understood issue with QUIC tests timing out when using this
        // version of simulated timers, I'm guessing that the event loops in question do not have a
        // real periodic timer that ensures that the max epoll wait ends up being relatively small
        // and ensures that events scheduled from outside the worker thread have a chance to
        // execute. It may be necessary to have the simulated scheduler add a real periodic timer on
        // construction in order to ensure that the event loop remains responsive to external event
        // activations.
        inc_pending = true;
        pending_dec_ = true;
      }
    }
    if (inc_pending) {
      time_system_.incPending();
    }
    if (!update_time_cb_->enabled()) {
      run_alarms_cb_->cancel();
      update_time_cb_->scheduleCallbackCurrentIteration();
    }
  }

private:
  void disableAlarmLockHeld(Alarm& alarm) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void updateTimeAndTriggerAlarms();
  void runReadyAlarms();

  struct AlarmRegistration {
    AlarmRegistration(MonotonicTime time, uint64_t randomness, Alarm& alarm)
        : time_(time), randomness_(randomness), alarm_(alarm) {}

    MonotonicTime time_;
    // Random tie-breaker for alarms scheduled for the same monotonic time used to mimic
    // non-deterministic execution of real alarms scheduled for the same wall time.
    uint64_t randomness_;
    Alarm& alarm_;

    friend bool operator<(const AlarmRegistration& lhs, const AlarmRegistration& rhs) {
      if (lhs.time_ != rhs.time_) {
        return lhs.time_ < rhs.time_;
      }
      if (lhs.randomness_ != rhs.randomness_) {
        return lhs.randomness_ < rhs.randomness_;
      }
      // Out of paranoia, use pointer comparison on the alarms as a final tie-breaker but also
      // ASSERT that this branch isn't hit in debug modes since in practice the randomness_
      // associated with two registrations should never be equal.
      ASSERT(false, "Alarm registration randomness_ for two alarms should never be equal.");
      return &lhs.alarm_ < &rhs.alarm_;
    }
  };

  class AlarmSet {
  public:
    bool empty() { return alarms_.empty(); }

    const AlarmRegistration& next() { return *alarms_.begin(); }

    void add(AlarmRegistration registration) {
      auto insert_result = alarms_.insert(registration);
      ASSERT(insert_result.second);
      alarm_registrations_map_.emplace(&registration.alarm_, insert_result.first);

      // Sanity check that the parallel data structures used for alarm registration have the same
      // number of entries.
      ASSERT(alarms_.size() == alarm_registrations_map_.size());
    }

    bool remove(Alarm& alarm) {
      auto it = alarm_registrations_map_.find(&alarm);
      if (it == alarm_registrations_map_.end()) {
        return false;
      }
      alarms_.erase(it->second);
      alarm_registrations_map_.erase(it);
      return true;
    }

    bool contains(Alarm& alarm) const {
      return alarm_registrations_map_.find(&alarm) != alarm_registrations_map_.end();
    }

  private:
    std::set<AlarmRegistration> alarms_;
    absl::flat_hash_map<Alarm*, std::set<AlarmRegistration>::const_iterator>
        alarm_registrations_map_;
  };

  absl::Mutex mutex_;
  bool not_running_cbs_ ABSL_GUARDED_BY(mutex_) = true;
  AlarmSet registered_alarms_ ABSL_GUARDED_BY(mutex_);
  AlarmSet triggered_alarms_ ABSL_GUARDED_BY(mutex_);

  MonotonicTime current_monotonic_time_ ABSL_GUARDED_BY(mutex_);
  SystemTime current_system_time_ ABSL_GUARDED_BY(mutex_);

  MonotonicTime next_monotonic_time_ ABSL_GUARDED_BY(mutex_);
  SystemTime next_system_time_ ABSL_GUARDED_BY(mutex_);

  TestRandomGenerator random_source_ ABSL_GUARDED_BY(mutex_);
  uint64_t legacy_next_idx_ ABSL_GUARDED_BY(mutex_) = 0;
  bool pending_dec_ ABSL_GUARDED_BY(mutex_) = false;

  SimulatedTimeSystemHelper& time_system_;
  CallbackScheduler& cb_scheduler_;
  SchedulableCallbackPtr update_time_cb_;
  SchedulableCallbackPtr run_alarms_cb_;
};

// Our simulated alarm inherits from TimerImpl so that the same dispatching
// mechanism used in RealTimeSystem timers is employed for simulated alarms.
class SimulatedTimeSystemHelper::Alarm : public Timer {
public:
  Alarm(SimulatedScheduler& simulated_scheduler, SimulatedTimeSystemHelper& time_system,
        CallbackScheduler& /*cb_scheduler*/, TimerCb cb)
      : cb_(cb), simulated_scheduler_(simulated_scheduler), time_system_(time_system) {}

  ~Alarm() override;

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& duration,
                   const ScopeTrackedObject* scope) override {
    enableHRTimer(duration, scope);
  };
  void enableHRTimer(const std::chrono::microseconds& duration,
                     const ScopeTrackedObject* scope) override;
  bool enabled() override { return simulated_scheduler_.isEnabled(*this); }

  SimulatedTimeSystemHelper& timeSystem() { return time_system_; }

  void runAlarm() { cb_(); }

private:
  TimerCb cb_;
  SimulatedScheduler& simulated_scheduler_;
  SimulatedTimeSystemHelper& time_system_;
};

absl::optional<MonotonicTime>
SimulatedTimeSystemHelper::SimulatedScheduler::minAlarmRegistrationTime() {
  absl::MutexLock lock(&mutex_);
  if (!triggered_alarms_.empty()) {
    if (!registered_alarms_.empty()) {
      return std::min(triggered_alarms_.next().time_, registered_alarms_.next().time_);
    }
    return triggered_alarms_.next().time_;
  }

  if (!registered_alarms_.empty()) {
    return registered_alarms_.next().time_;
  }

  return absl::nullopt;
}

TimerPtr SimulatedTimeSystemHelper::SimulatedScheduler::createTimer(const TimerCb& cb,
                                                                    Dispatcher& /*dispatcher*/) {
  return std::make_unique<SimulatedTimeSystemHelper::Alarm>(*this, time_system_, cb_scheduler_, cb);
}

bool SimulatedTimeSystemHelper::SimulatedScheduler::isEnabled(Alarm& alarm) {
  absl::MutexLock lock(&mutex_);
  return registered_alarms_.contains(alarm) || triggered_alarms_.contains(alarm);
}

void SimulatedTimeSystemHelper::SimulatedScheduler::enableAlarm(
    Alarm& alarm, const std::chrono::microseconds& duration) {
  {
    absl::MutexLock lock(&mutex_);
    if (duration.count() == 0 && triggered_alarms_.contains(alarm)) {
      return;
    } else if (Runtime::runtimeFeatureEnabled(
                   "envoy.reloadable_features.activate_timers_next_event_loop")) {
      disableAlarmLockHeld(alarm);
      registered_alarms_.add({current_monotonic_time_ + duration, random_source_.random(), alarm});
    } else {
      disableAlarmLockHeld(alarm);
      AlarmSet& alarm_set = (duration.count() != 0) ? registered_alarms_ : triggered_alarms_;
      alarm_set.add({current_monotonic_time_ + duration, ++legacy_next_idx_, alarm});
    }
  }

  if (duration.count() == 0) {
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.activate_timers_next_event_loop")) {
      run_alarms_cb_->scheduleCallbackNextIteration();
    } else {
      run_alarms_cb_->scheduleCallbackCurrentIteration();
    }
  }
}

void SimulatedTimeSystemHelper::SimulatedScheduler::disableAlarmLockHeld(Alarm& alarm) {
  if (triggered_alarms_.contains(alarm)) {
    ASSERT(!registered_alarms_.contains(alarm));
    triggered_alarms_.remove(alarm);
  } else {
    ASSERT(!triggered_alarms_.contains(alarm));
    registered_alarms_.remove(alarm);
  }
}

void SimulatedTimeSystemHelper::SimulatedScheduler::updateTimeAndTriggerAlarms() {
  bool dec_pending = false;
  {
    absl::MutexLock lock(&mutex_);
    current_monotonic_time_ = next_monotonic_time_;
    current_system_time_ = next_system_time_;
    if (pending_dec_) {
      dec_pending = true;
      pending_dec_ = false;
    }
  }
  run_alarms_cb_->cancel();
  runReadyAlarms();
  if (dec_pending) {
    time_system_.decPending();
  }
}

void SimulatedTimeSystemHelper::SimulatedScheduler::runReadyAlarms() {
  absl::MutexLock lock(&mutex_);
  auto monotonic_time = current_monotonic_time_;
  // TODO delay alarms scheduled this iteration until next iteration.
  while (!registered_alarms_.empty()) {
    const AlarmRegistration& alarm_registration = registered_alarms_.next();
    MonotonicTime alarm_time = alarm_registration.time_;
    if (alarm_time > monotonic_time) {
      break;
    }
    triggered_alarms_.add(alarm_registration);
    registered_alarms_.remove(alarm_registration.alarm_);
  }

  ASSERT(not_running_cbs_);
  not_running_cbs_ = false;
  while (!triggered_alarms_.empty()) {
    Alarm& alarm = triggered_alarms_.next().alarm_;
    triggered_alarms_.remove(alarm);
    UnlockGuard unlocker(mutex_);
    alarm.runAlarm();
  }
  ASSERT(!not_running_cbs_);
  not_running_cbs_ = true;
}

SimulatedTimeSystemHelper::Alarm::Alarm::~Alarm() {
  simulated_scheduler_.disableAlarm(*this);
  simulated_scheduler_.waitUntilIdle();
}

void SimulatedTimeSystemHelper::Alarm::Alarm::disableTimer() {
  simulated_scheduler_.disableAlarm(*this);
}

void SimulatedTimeSystemHelper::Alarm::Alarm::enableHRTimer(
    const std::chrono::microseconds& duration, const ScopeTrackedObject* /*scope*/) {
  simulated_scheduler_.enableAlarm(*this, duration);
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
        for (SimulatedScheduler* scheduler : schedulers_) {
          UnlockGuard unlocker(mutex_);
          absl::optional<MonotonicTime> min_alarm = scheduler->minAlarmRegistrationTime();
          if (min_alarm.has_value()) {
            next_wakeup = std::min(min_alarm.value(), next_wakeup);
          }
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

SchedulerPtr SimulatedTimeSystemHelper::createScheduler(Scheduler& /*base_scheduler*/,
                                                        CallbackScheduler& cb_scheduler) {
  return std::make_unique<SimulatedScheduler>(*this, cb_scheduler);
}

void SimulatedTimeSystemHelper::setMonotonicTimeLockHeld(const MonotonicTime& monotonic_time) {
  only_one_thread_.checkOneThread();
  // We don't have a MutexLock construct that allows temporarily
  // dropping the lock to run a callback. The main issue here is that we must
  // be careful not to be holding mutex_ when an exception can be thrown.
  // That can only happen here in alarm->activate(), which is run with the mutex
  // released.
  if (monotonic_time >= monotonic_time_) {
    system_time_ +=
        std::chrono::duration_cast<SystemTime::duration>(monotonic_time - monotonic_time_);
    monotonic_time_ = monotonic_time;
    // TODO release lock, protect by Await function that protects updating time boolean?
    for (SimulatedScheduler* scheduler : schedulers_) {
      UnlockGuard unlocker(mutex_);
      scheduler->updateTime(monotonic_time_, system_time_);
    }
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
