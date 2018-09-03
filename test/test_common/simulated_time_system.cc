#include <chrono>

#include "common/common/assert.h"
#include "common/event/event_impl_base.h"
#include "common/event/real_time_system.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {
namespace {

class SimulatedTimerFactory;

/**
 * An Alarm is created in the context of a thread's dispatcher.
 */
class Alarm : public Timer {
public:
  Alarm(SimulatedTimerFactory& time_factory, TimerCb cb, uint64_t index)
      : time_factory_(time_factory), cb_(cb), time_(0), index_(0) {
    ASSERT(cb_);
  }

  virtual ~Alarm() { ASSERT(time_ == 0); }

  // Timer
  void disableTimer() override;

  void enableTimer(const std::chrono::milliseconds& duration) override;

  // Compare two alarms, based on wakeup time and insertion order. Result
  // like strcmp (<0 for this < that, >0 for this > that), based on wakeup
  // time and index.
  int Compare(const Alarm* other) const {
    int cmp = 0;
    if (this != other) {
      if (wakeup_time_us_ < other->wakeup_time_us_) {
        cmp = -1;
      } else if (wakeup_time_us_ > other->wakeup_time_us_) {
        cmp = 1;
      } else if (index_ < other->index_) {
        cmp = -1;
      } else {
        DCHECK(index_ > other->index_);
        cmp = 1;
      }
    }
    return cmp;
  }

private:
  SimulatedTimerFactory& time_factory_;
  TimerCb cb_;
  MonotonicTime time_;
  uint64_t index_;
};

struct CompareAlarms {
  bool operator()(const Alarm* a, const Alarm* b) const { return a->Compare(b) < 0; }
};

// Each scheduler maintains its own timer
class SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(Libevent::BasePtr& libevent) : libevent_(libevent) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<TimerImpl>(libevent_, cb);
  };

  void removeAlarm(Alarm* alarm) { alarms_.erase(alarm); }

  void addAlarm(Alarm* alarm) { alarms_.insert(alarm); }

private:
  typedef std::set<Alarm*, CompareAlarms> AlarmSet;
  AlarmSet alarms_;
};

void Alarm::disableTimer() {
  time_factory_->removeAlarm(this);
  time_ = 0;
}

void Alarm::enableTimer(const std::chrono::milliseconds& duration) {
  if (d.count() == 0) {
    cb_();
  } else {
    time_factory_->addAlarm(this);
  }
}

} // namespace

// When we initialize our simulated time, we'll start the current time based on
// the real current time. But thereafter, real-time will not be used, and time
// will march forward only by calling sleep().
SimulatedTimeSystem::SimulatedTimeSystem()
    : monotonic_time_(real_time_source_.monotonicTime()),
      system_time_(real_time_source_.systemTime()) {}

TimerFactoryPtr SimulatedTimeSystem::createTimerFactory(Libevent::BasePtr& libevent) {
  return std::make_unique<SimulatedTimerFactory>(libevent);
}

void SimulatedTimeSystem::sleep(std::chrono::duration duration) {}

} // namespace Event
} // namespace Envoy
