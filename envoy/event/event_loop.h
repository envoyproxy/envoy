#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"

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
   * @param prepare_time_us monotonic time in microseconds at prepare.
   * @param timeout_set whether a timeout was set on the epoll wait.
   * @param timeout_us the duration of the timeout in microseconds if timeout_set is true.
   */
  virtual void onPrepare(uint64_t prepare_time_us, bool timeout_set, uint64_t timeout_us) = 0;

  /**
   * Called during epoll/event loop check phase (after waking up from polling for events).
   * @param check_time_us monotonic time in microseconds at check.
   */
  virtual void onCheck(uint64_t check_time_us) = 0;
};

using ObserverPtr = std::unique_ptr<Observer>;
using ObserverSharedPtr = std::shared_ptr<Observer>;

/**
 * Handle returned when registering an Evwatch::Observer with a Dispatcher.
 * When this handle is destructed (on any thread), the associated observer is automatically
 * unregistered and lazily pruned from the dispatcher's event loop.
 */
class ObserverHandle {
public:
  virtual ~ObserverHandle() = default;
};

using ObserverHandlePtr = std::unique_ptr<ObserverHandle>;

} // namespace Evwatch
} // namespace Event
} // namespace Envoy
