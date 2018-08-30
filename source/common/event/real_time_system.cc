#include "common/event/real_time_system.h"

#include <chrono>

#include "common/common/assert.h"
#include "common/event/event_impl_base.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {
namespace {

/**
 * libevent implementation of Timer.
 */
class TimerImpl : public Timer, ImplBase {
public:
  TimerImpl(Libevent::BasePtr& libevent, TimerCb cb) : cb_(cb) {
    ASSERT(cb_);
    evtimer_assign(
        &raw_event_, libevent.get(),
        [](evutil_socket_t, short, void* arg) -> void { static_cast<TimerImpl*>(arg)->cb_(); },
        this);
  }

  // Timer
  void disableTimer() override { event_del(&raw_event_); }
  void enableTimer(const std::chrono::milliseconds& d) override {
    if (d.count() == 0) {
      event_active(&raw_event_, EV_TIMEOUT, 0);
    } else {
      std::chrono::microseconds us = std::chrono::duration_cast<std::chrono::microseconds>(d);
      timeval tv;
      tv.tv_sec = us.count() / 1000000;
      tv.tv_usec = us.count() % 1000000;
      event_add(&raw_event_, &tv);
    }
  }

private:
  TimerCb cb_;
};

class RealScheduler : public Scheduler {
public:
  RealScheduler(Libevent::BasePtr& libevent) : libevent_(libevent) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<TimerImpl>(libevent_, cb);
  };

private:
  Libevent::BasePtr& libevent_;
};

} // namespace

SchedulerPtr RealTimeSystem::createScheduler(Libevent::BasePtr& libevent) {
  return std::make_unique<RealScheduler>(libevent);
}

} // namespace Event
} // namespace Envoy
