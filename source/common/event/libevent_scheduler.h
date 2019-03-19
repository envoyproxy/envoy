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

  // Scheduler
  TimerPtr createTimer(const TimerCb& cb) override;

  /**
   * Runs the libevent loop once, without blocking.
   */
  void nonBlockingLoop();

  /**
   * Runs the libevent loop once, with block.
   */
  void blockingLoop();

  /**
   * Exits the libevent loop.
   */
  void loopExit();

  /**
   * TODO(jmarantz): consider strengthening this abstraction and instead of
   * exposing the libevent base pointer, provide API abstractions for the calls
   * into it. Among other benefits this might make it more tractable to someday
   * consider an alternative to libevent if the need arises.
   *
   * @return the underlying libevent structure.
   */
  event_base& base() { return *libevent_; }

private:
  Libevent::BasePtr libevent_;
};

} // namespace Event
} // namespace Envoy
