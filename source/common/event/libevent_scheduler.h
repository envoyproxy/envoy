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
   * Executes any events that have been activated, then exit.
   */
  void runActivatedEvents();

  /**
   * Waits for any pending events to activate, executes them, then exits. Exits
   * immediately if there are no pending or active events.
   */
  void runUntilEmpty();

  /**
   * Runs the event-loop until loopExit() is called, blocking until there
   * are pending or active events.
   */
  void runUntilExit();

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
