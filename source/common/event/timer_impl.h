#pragma once

#include "event_impl_base.h"

#include "envoy/event/timer.h"

namespace Event {

/**
 * libevent implementation of Event::Timer.
 */
class TimerImpl : public Timer, ImplBase {
public:
  TimerImpl(DispatcherImpl& dispatcher, TimerCb cb);

  // Event::Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& d) override;

private:
  TimerCb cb_;
};

} // Event
