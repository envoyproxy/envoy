#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

namespace Envoy {
namespace Event {
namespace Evwatch {

/**
 * An interface for observing timing and latency metrics across event loop iterations on
 * dispatchers. This hook is invoked by DispatcherImpl on every epoll check and prepare cycle.
 */
class Observer {
public:
  virtual ~Observer() = default;

  /**
   * Called during epoll/event loop prepare phase (before sleeping/polling for events).
   * @param prepare_time monotonic time at prepare.
   * @param timeout_set whether a timeout was set on the epoll wait.
   * @param timeout the duration of the timeout if timeout_set is true.
   */
  virtual void onPrepare(MonotonicTime prepare_time, bool timeout_set,
                         std::chrono::microseconds timeout) = 0;

  /**
   * Called during epoll/event loop check phase (after waking up from polling for events).
   * @param check_time monotonic time at check.
   */
  virtual void onCheck(MonotonicTime check_time) = 0;
};

using ObserverPtr = std::unique_ptr<Observer>;
using ObserverSharedPtr = std::shared_ptr<Observer>;
using ObserverWeakPtr = std::weak_ptr<Observer>;

/**
 * Handle returned when registering an Evwatch::Observer with a Dispatcher.
 * When this handle is destructed (on any thread), the associated observer is automatically
 * unregistered and lazily pruned from the dispatcher's event loop.
 */
class ObserverHandle {
public:
  virtual ~ObserverHandle() = default;

  /**
   * Returns a weak reference to the underlying observer. This allows ad-hoc tracking lists
   * (such as periodic metric pollers) to safely reference the observer without interfering
   * with RAII handle ownership and lifecycle unregistration.
   */
  virtual ObserverWeakPtr observer() const = 0;
};

using ObserverHandlePtr = std::unique_ptr<ObserverHandle>;

} // namespace Evwatch
} // namespace Event
} // namespace Envoy
