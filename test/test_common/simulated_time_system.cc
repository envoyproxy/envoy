#include <chrono>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/event/real_time_system.h"
#include "common/event/timer_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

// Our simulated alarm inherits from TimerImpl so that the same dispatching
// mechanism used in RealTimeSystem timers is employed for simulated alarms.
// using libevent's event_active(). Note that libevent is placed into
// thread-safe mode due to the call to evthread_use_pthreads() in
// source/common/event/libevent.cc.
class SimulatedTimeSystem::Alarm : public TimerImpl {
public:
  Alarm(SimulatedTimeSystem& time_system, Libevent::BasePtr& libevent, TimerCb cb)
      : TimerImpl(libevent, cb),
        time_system_(time_system), index_(time_system.nextIndex()),
        armed_(false) {
  }

  virtual ~Alarm() { ASSERT(!armed_); }

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& duration) override;

  // Compare two alarms, based on wakeup time and insertion order. Result
  // like strcmp (<0 for this < that, >0 for this > that), based on wakeup
  // time and index.
  int Compare(const Alarm* other) const {
    int cmp = 0;
    if (this != other) {
      if (time_ < other->time_) {
        cmp = -1;
      } else if (time_ > other->time_) {
        cmp = 1;
      } else if (index_ < other->index_) {
        cmp = -1;
      } else {
        ASSERT(index_ > other->index_);
        cmp = 1;
      }
    }
    return cmp;
  }

  void setTime(MonotonicTime time) { time_ = time; }
  void run() {
    armed_ = false;
    std::chrono::milliseconds duration = std::chrono::milliseconds::zero();
    TimerImpl::enableTimer(duration);
  }
  MonotonicTime time() const { ASSERT(armed_); return time_; }
  uint64_t index() const { return index_; }

private:
  SimulatedTimeSystem& time_system_;
  MonotonicTime time_;
  uint64_t index_;
  bool armed_;
};

// Compare two alarms, based on wakeup time and insertion order. Returns true if
// a comes before b.

// like strcmp (<0 for a < b, >0 for a > b), based on wakeup time and index.
bool SimulatedTimeSystem::CompareAlarms::operator()(const Alarm* a, const Alarm* b) const {
  if (a != b) {
    if (a->time() < b->time()) {
      return true;
    } else if (a->time() == b->time() && a->index() < b->index()) {
      return true;
    }
  }
  return false;
};

namespace {

// Each scheduler maintains its own timer
class SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(SimulatedTimeSystem& time_system, Libevent::BasePtr& libevent)
      : time_system_(time_system), libevent_(libevent) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<SimulatedTimeSystem::Alarm>(time_system_, libevent_, cb);
  };

 private:
  SimulatedTimeSystem& time_system_;
  Libevent::BasePtr& libevent_;
};

} // namespace

void SimulatedTimeSystem::Alarm::disableTimer() {
  ASSERT(armed_);
  time_system_.removeAlarm(this);
  armed_ = false;
}

void SimulatedTimeSystem::Alarm::enableTimer(const std::chrono::milliseconds& duration) {
  ASSERT(!armed_);
  armed_ = true;
  if (duration.count() == 0) {
    run();
  } else {
    time_system_.addAlarm(this, duration);
  }
}

// When we initialize our simulated time, we'll start the current time based on
// the real current time. But thereafter, real-time will not be used, and time
// will march forward only by calling sleep().
SimulatedTimeSystem::SimulatedTimeSystem()
    : monotonic_time_(real_time_source_.monotonicTime()),
      system_time_(real_time_source_.systemTime()),
      index_(0) {}

SystemTime SimulatedTimeSystem::systemTime() {
  Thread::LockGuard lock(mutex_);
  return system_time_;
}

MonotonicTime SimulatedTimeSystem::monotonicTime() {
  Thread::LockGuard lock(mutex_);
  return monotonic_time_;
}

int64_t SimulatedTimeSystem::nextIndex() {
  Thread::LockGuard lock(mutex_);
  return index_++;
}

void SimulatedTimeSystem::addAlarm(Alarm* alarm, const std::chrono::milliseconds& duration) {
  Thread::LockGuard lock(mutex_);
  alarm->setTime(monotonic_time_ + duration);
  alarms_.insert(alarm);
}

void SimulatedTimeSystem::removeAlarm(Alarm* alarm) {
  Thread::LockGuard lock(mutex_);
  alarms_.erase(alarm);
}

SchedulerPtr SimulatedTimeSystem::createScheduler(Libevent::BasePtr& libevent) {
  return std::make_unique<SimulatedScheduler>(*this, libevent);
}

std::vector<SimulatedTimeSystem::Alarm*> SimulatedTimeSystem::findReadyAlarmsLockHeld() {
  std::vector<Alarm*> ready;
  while (!alarms_.empty()) {
    AlarmSet::iterator p = alarms_.begin();
    Alarm* alarm = *p;
    if (alarm->time() > monotonic_time_) {
      break;
    }
    alarms_.erase(p);
    ready.push_back(alarm);  // Don't fire alarms under lock.
  }
  return ready;
}

void SimulatedTimeSystem::runAlarms(std::vector<Alarm*> ready) {
  for (Alarm* alarm : ready) {
    alarm->run();
  }
}

} // namespace Event
} // namespace Envoy
