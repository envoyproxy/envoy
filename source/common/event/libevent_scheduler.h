#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/event/timer.h"

#include "source/common/event/libevent.h"

#include "event2/event.h"
#include "event2/watch.h"

namespace Envoy {
namespace Event {

// Implements Scheduler based on libevent.
//
// Here is a rough summary of operations that libevent performs in each event loop iteration, in
// order. Note that the invocation order for "same-iteration" operations that execute as a group
// can be surprising and invocation order of expired timers is non-deterministic.
// Whenever possible, it is preferable to avoid making event invocation ordering assumptions.
//
// 1. Calculate the poll timeout by comparing the current time to the deadline of the closest
// timer (the one at head of the priority queue).
// 2. Run registered "prepare" callbacks.
// 3. Poll for fd events using the closest timer as timeout, add active fds to the work list.
// 4. Run registered "check" callbacks.
// 5. Check timer deadlines against current time and move expired timers from the timer priority
// queue to the work list. Expired timers are moved to the work list is a non-deterministic order.
// 6. Execute items in the work list until the list is empty. Note that additional work
// items could be added to the work list during execution of this step, more details below.
// 7. Goto 1 if the loop termination condition has not been reached
//
// The following "same-iteration" work items are added directly to the work list when they are
// scheduled so they execute in the current iteration of the event loop. Note that there are no
// ordering guarantees when mixing the mechanisms below. Specifically, it is unsafe to assume that
// calling post followed by deferredDelete will result in the post callback being invoked before the
// deferredDelete; deferredDelete will run first if there is a pending deferredDeletion at the time
// the post callback is scheduled because deferredDelete invocation is grouped.
// - Event::Dispatcher::post(cb). Post callbacks are invoked as a group.
// - Event::Dispatcher::deferredDelete(object) and Event::DeferredTaskUtil::deferredRun(...).
// The same mechanism implements both of these operations, so they are invoked as a group.
// - Event::SchedulableCallback::scheduleCallbackCurrentIteration(). Each of these callbacks is
// scheduled and invoked independently.
//
// Event::FileEvent::activate and Event::SchedulableCallback::scheduleCallbackNextIteration are
// implemented as libevent timers with a deadline of 0. Both of these actions are moved to the work
// list while checking for expired timers during step 5.
//
// Events execute in the following order, derived from the order in which items were added to the
// work list:
// 0. Events added via event_active prior to the start of the event loop (in tests)
// 1. Fd events
// 2. Timers, FileEvent::activate and SchedulableCallback::scheduleCallbackNextIteration
// 3. "Same-iteration" work items described above, including Event::Dispatcher::post callbacks
class LibeventScheduler : public Scheduler, public CallbackScheduler {
public:
  using OnPrepareCallback = std::function<void()>;
  using OnCheckCallback = std::function<void()>;

  LibeventScheduler();

  // Scheduler
  TimerPtr createTimer(const TimerCb& cb, Dispatcher& dispatcher) override;
  SchedulableCallbackPtr createSchedulableCallback(const std::function<void()>& cb) override;

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
   * Register callback to be called in the event loop prior to polling for
   * events. Must not be called more than once. |callback| must not be null.
   * |callback| cannot be unregistered, therefore it has to be valid throughout
   * the lifetime of |this|.
   */
  void registerOnPrepareCallback(OnPrepareCallback&& callback);

  /**
   * Register callback to be called in the event loop after polling for
   * events and prior to handling those events. Must not be called more than once. |callback| must
   * not be null. |callback| cannot be unregistered, therefore it has to be valid throughout the
   * lifetime of |this|.
   */
  void registerOnCheckCallback(OnCheckCallback&& callback);

  /**
   * Start writing stats once thread-local storage is ready to receive them (see
   * ThreadLocalStoreImpl::initializeThreading).
   */
  void initializeStats(DispatcherStats* stats);

private:
  static void onPrepareForCallback(evwatch*, const evwatch_prepare_cb_info* info, void* arg);
  static void onCheckForCallback(evwatch*, const evwatch_check_cb_info* info, void* arg);
  static void onPrepareForStats(evwatch*, const evwatch_prepare_cb_info* info, void* arg);
  static void onCheckForStats(evwatch*, const evwatch_check_cb_info*, void* arg);

  static constexpr int flagsBasedOnEventType() {
    if constexpr (Event::PlatformDefaultTriggerType == FileTriggerType::Level) {
      // With level events, EVLOOP_NONBLOCK will cause the libevent event_base_loop to run
      // forever. This is because the write event callbacks will trigger every time through the
      // loop. Adding EVLOOP_ONCE ensures the loop will run at most once
      return EVLOOP_NONBLOCK | EVLOOP_ONCE;
    }
    return EVLOOP_NONBLOCK;
  }

  Libevent::BasePtr libevent_;
  DispatcherStats* stats_{}; // stats owned by the containing DispatcherImpl
  bool timeout_set_{};       // whether there is a poll timeout in the current event loop iteration
  timeval timeout_{};        // the poll timeout for the current event loop iteration, if available
  timeval prepare_time_{};   // timestamp immediately before polling
  timeval check_time_{};     // timestamp immediately after polling
  OnPrepareCallback prepare_callback_; // callback to be called from onPrepareForCallback()
  OnCheckCallback check_callback_;     // callback to be called from onCheckForCallback()
};

} // namespace Event
} // namespace Envoy
