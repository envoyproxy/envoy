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
  SchedulableCallback() = default;
  virtual ~SchedulableCallback() = default;

  /**
   * Schedule the callback so it runs in the current iteration of the event loop.
   */
  virtual void scheduleCallback() PURE;
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
