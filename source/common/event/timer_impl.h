#pragma once

#include <chrono>

#include "envoy/event/timer.h"

#include "common/event/event_impl_base.h"
#include "common/event/libevent.h"

namespace Envoy {
namespace Event {

/**
 * libevent implementation of Timer.
 */
class TimerImpl : public Timer, ImplBase {
public:
  TimerImpl(Libevent::BasePtr& libevent, TimerCb cb);

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& d) override;
  bool enabled() override;

  // Public for testing.
  void millisecondsToTimeval(timeval* tv, const std::chrono::milliseconds& d);

private:
  TimerCb cb_;
};

} // namespace Event
} // namespace Envoy
