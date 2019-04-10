#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "common/event/libevent.h"

#include "event2/event.h"
#include "event2/watch.h"

namespace Envoy {
namespace Event {

// Implements Scheduler based on libevent.
class LibeventScheduler : public Scheduler {
public:
  LibeventScheduler();

  // Scheduler
  TimerPtr createTimer(const TimerCb& cb) override;

  /**
   * Runs the event loop.
   *
   * @param mode The mode in which to run the event loop.
   */
  void run(Dispatcher::RunType mode);

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

  /**
   * Start writing stats once thread-local storage is ready to receive them (see
   * ThreadLocalStoreImpl::initializeThreading).
   */
  void initializeStats(DispatcherStats* stats_);

private:
  static void onPrepare(evwatch*, const evwatch_prepare_cb_info* info, void* arg);
  static void onCheck(evwatch*, const evwatch_check_cb_info*, void* arg);

  Libevent::BasePtr libevent_;
  DispatcherStats* stats_{}; // stats owned by the containing DispatcherImpl
  bool timeout_set_{};       // whether there is a poll timeout in the current event loop iteration
  timeval timeout_{};        // the poll timeout for the current event loop iteration, if available
  timeval prepare_time_{};   // timestamp immediately before polling
  timeval check_time_{};     // timestamp immediately after polling
};

} // namespace Event
} // namespace Envoy
