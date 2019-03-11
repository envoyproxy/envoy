#pragma once

#include "envoy/event/timer.h"

#include "common/event/libevent.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

// Implements Scheduler based on libevent.
class LibeventScheduler : public Scheduler {
public:
  LibeventScheduler();
  TimerPtr createTimer(const TimerCb& cb) override;
  void nonBlockingLoop();
  void blockingLoop();
  void loopExit();
  event_base& base() { return *libevent_; }

private:
  Libevent::BasePtr libevent_;
};

} // namespace Event
} // namespace Envoy
