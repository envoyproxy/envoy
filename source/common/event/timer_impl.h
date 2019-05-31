#pragma once

#include <chrono>

#include "envoy/event/timer.h"

#include "common/event/event_impl_base.h"
#include "common/event/libevent.h"

namespace Envoy {
namespace Event {

/**
 * Utility helper functions for Timer implementation.
 */
class TimerUtils {
public:
  static void millisecondsToTimeval(const std::chrono::milliseconds& d, timeval& tv);
};

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

private:
  TimerCb cb_;
};

} // namespace Event
} // namespace Envoy
