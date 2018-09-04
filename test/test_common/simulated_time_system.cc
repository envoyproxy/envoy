#include <chrono>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/event/event_impl_base.h"
#include "common/event/real_time_system.h"

#include "test/test_common/simulated_time_system.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

/**
 * An Alarm is created in the context of a thread's dispatcher.
 */
class SimulatedTimeSystem::Alarm : public Timer {
public:
  Alarm(SimulatedTimeSystem& time_system, Dispatcher& dispatcher, TimerCb cb)
      : time_system_(time_system), dispatcher_(dispatcher), cb_(cb),
        index_(time_system.nextIndex()), armed_(false) {
    ASSERT(cb_);
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

  void run() { armed_ = false; dispatcher_.post(cb_); }
  MonotonicTime time() const { ASSERT(armed_); return time_; }

private:
  SimulatedTimeSystem& time_system_;
  Dispatcher& dispatcher_;
  TimerCb cb_;
  MonotonicTime time_;
  uint64_t index_;
  bool armed_;
};

bool SimulatedTimeSystem::CompareAlarms::operator()(const Alarm* a, const Alarm* b) const {
  return a->Compare(b) < 0;
};

namespace {

// Each scheduler maintains its own timer
class SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(SimulatedTimeSystem& time_system, Dispatcher& dispatcher)
      : time_system_(time_system), dispatcher_(dispatcher) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<SimulatedTimeSystem::Alarm>(time_system_, dispatcher_, cb);
  };

 private:
  SimulatedTimeSystem& time_system_;
  Dispatcher& dispatcher_;
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
    time_system_.addAlarm(this);
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

void SimulatedTimeSystem::addAlarm(Alarm* alarm) {
  Thread::LockGuard lock(mutex_);
  alarms_.insert(alarm);
}

void SimulatedTimeSystem::removeAlarm(Alarm* alarm) {
  Thread::LockGuard lock(mutex_);
  alarms_.erase(alarm);
}

SchedulerPtr SimulatedTimeSystem::createScheduler(Libevent::BasePtr&, Dispatcher& dispatcher) {
  return std::make_unique<SimulatedScheduler>(*this, dispatcher);
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
    ready.push_back(alarm);  // Don't fine the alarms under lock.
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
