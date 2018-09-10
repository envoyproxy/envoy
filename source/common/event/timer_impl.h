#pragma once

#include <chrono>

#include "envoy/event/timer.h"

#include "common/event/event_impl_base.h"

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

private:
  TimerCb cb_;
};

} // namespace Event
} // namespace Envoy
