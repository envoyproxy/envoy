#include "test/test_common/simulated_time_system.h"

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"
#include "source/common/event/real_time_system.h"
#include "source/common/event/timer_impl.h"
#include "source/common/runtime/runtime_features.h"

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
        thread_factory_(Thread::threadFactoryForTest()),
        run_alarms_cb_(cb_scheduler.createSchedulableCallback([this] { runReadyAlarms(); })),
        monotonic_time_(time_system_.monotonicTime()), system_time_(time_system_.systemTime()) {
    time_system_.registerScheduler(this);
  }
  ~SimulatedScheduler() override { time_system_.unregisterScheduler(this); }

  // From Scheduler.
  TimerPtr createTimer(const TimerCb& cb, Dispatcher& /*dispatcher*/) override;

  // Implementation of SimulatedTimeSystemHelper::Alarm methods.
  bool isEnabled(Alarm& alarm) ABSL_LOCKS_EXCLUDED(mutex_);
  void enableAlarm(Alarm& alarm, const std::chrono::microseconds duration)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void disableAlarm(Alarm& alarm) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    disableAlarmLockHeld(alarm);
    // Wait until alarm processing for the current thread completes when disabling from outside the
    // event loop thread. This helps avoid data races when deleting Alarm objects from outside the
    // event loop thread.
    if (running_cbs_ && !thread_advancing_time_.isEmpty() &&
        thread_advancing_time_ != thread_factory_.currentThreadId()) {
      waitForNoRunningCallbacksLockHeld();
    }
  }

  // Called by SimulatedTimeSystemHelper::setMonotonicTime to update the time associated with each
  // of the simulated schedulers and associated alarms.
  void updateTime(MonotonicTime monotonic_time, SystemTime system_time)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    bool inc_pending = false;
    {
      absl::MutexLock lock(&mutex_);
      // Wait until the event loop associated with this scheduler is not executing callbacks so time
      // does not change in the middle of a callback.
      waitForNoRunningCallbacksLockHeld();
      monotonic_time_ = monotonic_time;
      system_time_ = system_time;
      if (!pending_dec_ && (!registered_alarms_.empty() || !triggered_alarms_.empty())) {
        // Selectively increment the pending updates counter only on dispatchers that have active
        // alarms to allow advanceTimeWait to work but avoid getting stuck if some of the event
        // loops associated with some of the registered simulated schedulers is not currently
        // active.
        inc_pending = true;
        pending_dec_ = true;
      }
    }
    if (inc_pending) {
      time_system_.incPending();
    }

    if (!run_alarms_cb_->enabled()) {
      run_alarms_cb_->scheduleCallbackNextIteration();
    }
  }

private:
  void waitForNoRunningCallbacksLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    mutex_.Await(absl::Condition(
        +[](bool* running_cbs) -> bool { return !*running_cbs; }, &running_cbs_));
  }

  void disableAlarmLockHeld(Alarm& alarm) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Collect expired alarms and execute associated callbacks.
  void runReadyAlarms() ABSL_LOCKS_EXCLUDED(mutex_);

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
    bool empty() const { return sorted_alarms_.empty(); }

    const AlarmRegistration& next() const {
      ASSERT(!empty());
      return *sorted_alarms_.begin();
    }

    void add(AlarmRegistration registration) {
      auto insert_result = sorted_alarms_.insert(registration);
      ASSERT(insert_result.second);
      alarm_registrations_map_.emplace(&registration.alarm_, insert_result.first);

      // Sanity check that the parallel data structures used for alarm registration have the same
      // number of entries.
      ASSERT(sorted_alarms_.size() == alarm_registrations_map_.size());
    }

    bool remove(Alarm& alarm) {
      auto it = alarm_registrations_map_.find(&alarm);
      if (it == alarm_registrations_map_.end()) {
        return false;
      }
      sorted_alarms_.erase(it->second);
      alarm_registrations_map_.erase(it);

      // Sanity check that the parallel data structures used for alarm registration have the same
      // number of entries.
      ASSERT(sorted_alarms_.size() == alarm_registrations_map_.size());
      return true;
    }

    bool contains(Alarm& alarm) const {
      return alarm_registrations_map_.find(&alarm) != alarm_registrations_map_.end();
    }

  private:
    std::set<AlarmRegistration> sorted_alarms_;
    absl::flat_hash_map<Alarm*, std::set<AlarmRegistration>::const_iterator>
        alarm_registrations_map_;
  };

  SimulatedTimeSystemHelper& time_system_;
  CallbackScheduler& cb_scheduler_;
  Thread::ThreadFactory& thread_factory_;
  SchedulableCallbackPtr run_alarms_cb_;

  absl::Mutex mutex_;
  bool running_cbs_ ABSL_GUARDED_BY(mutex_) = false;
  AlarmSet registered_alarms_ ABSL_GUARDED_BY(mutex_);
  AlarmSet triggered_alarms_ ABSL_GUARDED_BY(mutex_);

  MonotonicTime monotonic_time_ ABSL_GUARDED_BY(mutex_);
  SystemTime system_time_ ABSL_GUARDED_BY(mutex_);

  // Id of the thread where the event loop is running.
  Thread::ThreadId thread_advancing_time_ ABSL_GUARDED_BY(mutex_);
  // True if the SimulatedTimeSystemHelper is waiting for the scheduler to process expired alarms
  // and call decPending after an update to monotonic time.
  bool pending_dec_ ABSL_GUARDED_BY(mutex_) = false;
  // Used to randomize the ordering of alarms scheduled for the same time. This mimics the trigger
  // order of real timers scheduled for the same absolute time is non-deterministic.
  // Each simulated scheduler has it's own TestRandomGenerator with the same seed to improve test
  // failure reproducibility when running against a specific seed by minimizing cross scheduler
  // interactions.
  TestRandomGenerator random_source_ ABSL_GUARDED_BY(mutex_);
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
  void enableTimer(const std::chrono::milliseconds duration,
                   const ScopeTrackedObject* scope) override {
    enableHRTimer(duration, scope);
  };
  void enableHRTimer(const std::chrono::microseconds duration,
                     const ScopeTrackedObject* scope) override;
  bool enabled() override { return simulated_scheduler_.isEnabled(*this); }

  SimulatedTimeSystemHelper& timeSystem() { return time_system_; }

  void runAlarm() { cb_(); }

private:
  TimerCb cb_;
  SimulatedScheduler& simulated_scheduler_;
  SimulatedTimeSystemHelper& time_system_;
};

TimerPtr SimulatedTimeSystemHelper::SimulatedScheduler::createTimer(const TimerCb& cb,
                                                                    Dispatcher& /*dispatcher*/) {
  return std::make_unique<SimulatedTimeSystemHelper::Alarm>(*this, time_system_, cb_scheduler_, cb);
}

bool SimulatedTimeSystemHelper::SimulatedScheduler::isEnabled(Alarm& alarm) {
  absl::MutexLock lock(&mutex_);
  return registered_alarms_.contains(alarm) || triggered_alarms_.contains(alarm);
}

void SimulatedTimeSystemHelper::SimulatedScheduler::enableAlarm(
    Alarm& alarm, const std::chrono::microseconds duration) {
  {
    absl::MutexLock lock(&mutex_);
    if (duration.count() == 0 && triggered_alarms_.contains(alarm)) {
      return;
    } else {
      disableAlarmLockHeld(alarm);
      registered_alarms_.add({monotonic_time_ + duration, random_source_.random(), alarm});
    }
  }

  if (duration.count() == 0) {
    run_alarms_cb_->scheduleCallbackNextIteration();
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

void SimulatedTimeSystemHelper::SimulatedScheduler::runReadyAlarms() {
  bool dec_pending = false;
  {
    absl::MutexLock lock(&mutex_);
    if (pending_dec_) {
      dec_pending = true;
      pending_dec_ = false;
    }
    if (thread_advancing_time_.isEmpty()) {
      thread_advancing_time_ = thread_factory_.currentThreadId();
    } else {
      ASSERT(thread_advancing_time_ == thread_factory_.currentThreadId());
    }
    auto monotonic_time = monotonic_time_;
    while (!registered_alarms_.empty()) {
      const AlarmRegistration& alarm_registration = registered_alarms_.next();
      MonotonicTime alarm_time = alarm_registration.time_;
      if (alarm_time > monotonic_time) {
        break;
      }
      triggered_alarms_.add(alarm_registration);
      registered_alarms_.remove(alarm_registration.alarm_);
    }

    ASSERT(!running_cbs_);
    running_cbs_ = true;
    while (!triggered_alarms_.empty()) {
      Alarm& alarm = triggered_alarms_.next().alarm_;
      triggered_alarms_.remove(alarm);
      UnlockGuard unlocker(mutex_);
      alarm.runAlarm();
    }
    ASSERT(running_cbs_);
    ASSERT(monotonic_time == monotonic_time_);
    running_cbs_ = false;
  }
  if (dec_pending) {
    time_system_.decPending();
  }
}

SimulatedTimeSystemHelper::Alarm::Alarm::~Alarm() { simulated_scheduler_.disableAlarm(*this); }

void SimulatedTimeSystemHelper::Alarm::Alarm::disableTimer() {
  simulated_scheduler_.disableAlarm(*this);
}

void SimulatedTimeSystemHelper::maybeLogTimerWarning() {
  if (++warning_logged_ == 1) {
    ENVOY_LOG_MISC(warn, "Simulated timer enabled. Use advanceTimeWait or "
                         "advanceTimeAsync functions to ensure it is called.");
  }
}

void SimulatedTimeSystemHelper::Alarm::Alarm::enableHRTimer(
    const std::chrono::microseconds duration, const ScopeTrackedObject* /*scope*/) {
  time_system_.maybeLogTimerWarning();
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
// will march forward only by calling advanceTimeAndRun() or advanceTimeWait().
SimulatedTimeSystemHelper::SimulatedTimeSystemHelper()
    : monotonic_time_(MonotonicTime(std::chrono::seconds(0))),
      system_time_(real_time_source_.systemTime()) {
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

void SimulatedTimeSystemHelper::advanceTimeAsyncImpl(const Duration& duration) {
  only_one_thread_.checkOneThread();
  absl::MutexLock lock(&mutex_);
  MonotonicTime monotonic_time =
      monotonic_time_ + std::chrono::duration_cast<MonotonicTime::duration>(duration);
  setMonotonicTimeLockHeld(monotonic_time);
}

void SimulatedTimeSystemHelper::advanceTimeWaitImpl(const Duration& duration) {
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
      +[](const uint32_t* pending_updates) -> bool { return *pending_updates == 0; },
      &pending_updates_));
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
