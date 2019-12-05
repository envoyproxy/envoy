#pragma once

#include <chrono>

#include "envoy/event/timer.h"

#include "common/common/scope_tracker.h"
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
  TimerImpl(Libevent::BasePtr& libevent, TimerCb cb, Event::Dispatcher& dispatcher);

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& d, const ScopeTrackedObject* scope) override;
  bool enabled() override;

private:
  TimerCb cb_;
  Dispatcher& dispatcher_;
  // This has to be atomic for alarms which are handled out of thread, for
  // example if the DispatcherImpl::post is called by two threads, they race to
  // both set this to null.
  std::atomic<const ScopeTrackedObject*> object_{};
};

} // namespace Event
} // namespace Envoy
