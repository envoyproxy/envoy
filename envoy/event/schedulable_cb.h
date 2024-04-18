#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Event {

/**
 * Callback wrapper that allows direct scheduling of callbacks in the event loop.
 */
class SchedulableCallback {
public:
  virtual ~SchedulableCallback() = default;

  /**
   * Schedule the callback so it runs in the current iteration of the event loop after all events
   * scheduled in the current event loop have had a chance to execute.
   */
  virtual void scheduleCallbackCurrentIteration() PURE;

  /**
   * Schedule the callback so it runs in the next iteration of the event loop. There are no
   * ordering guarantees for callbacks scheduled for the next iteration, not even among
   * next-iteration callbacks.
   */
  virtual void scheduleCallbackNextIteration() PURE;

  /**
   * Cancel pending execution of the callback.
   */
  virtual void cancel() PURE;

  /**
   * Return true whether the SchedulableCallback is scheduled for execution.
   */
  virtual bool enabled() PURE;
};

using SchedulableCallbackPtr = std::unique_ptr<SchedulableCallback>;

/**
 * SchedulableCallback factory.
 */
class CallbackScheduler {
public:
  virtual ~CallbackScheduler() = default;

  /**
   * Create a schedulable callback.
   */
  virtual SchedulableCallbackPtr createSchedulableCallback(const std::function<void()>& cb) PURE;
};

} // namespace Event
} // namespace Envoy
