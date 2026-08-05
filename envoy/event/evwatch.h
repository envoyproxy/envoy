#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "envoy/common/time.h"

namespace Envoy {
namespace Event {
namespace Evwatch {

/**
 * An interface for observing timing and latency metrics across event loop iterations on
 * dispatchers. This hook is invoked by LibeventScheduler on every epoll check and prepare cycle.
 */
class Observer {
public:
  virtual ~Observer() = default;

  /**
   * Called during epoll/event loop prepare phase (before sleeping/polling for events).
   * @param prepare_time monotonic time at prepare.
   * @param timeout the duration of the timeout if a timeout was set on the epoll wait.
   */
  virtual void onPrepare(MonotonicTime prepare_time,
                         std::optional<MonotonicTime::duration> timeout) = 0;

  /**
   * Called during epoll/event loop check phase (after waking up from polling for events).
   * @param check_time monotonic time at check.
   */
  virtual void onCheck(MonotonicTime check_time) = 0;

  /**
   * Called on the dispatcher thread when the observer is unregistered from the dispatcher
   * or when the dispatcher loop is shutting down.
   */
  virtual void onClose() {}
};

} // namespace Evwatch
} // namespace Event
} // namespace Envoy
