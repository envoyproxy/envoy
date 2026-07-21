#pragma once

#include <coroutine>
#include <functional>

#include "envoy/common/pure.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Coroutine {

/**
 * Minimal scheduling interface for coroutines: enough to resume (or start) a
 * suspended coroutine handle "soon", and to create timers.
 *
 * The interface is intentionally small so that `DispatcherExecutor` (backed by an
 * `Event::Dispatcher`), a test `ManualExecutor`, and any future foreign executor
 * all satisfy it. Keeping it minimal also keeps the door open to modeling the
 * `std::execution::scheduler` concept once C++26 lands.
 */
class Executor {
public:
  virtual ~Executor() = default;

  /**
   * Resume (or start) a coroutine on this executor's run loop. On a `Dispatcher`
   * this is a `post()`, which is thread-safe -- this is the seam that makes
   * cross-thread hopping possible later without changing callers.
   *
   * Note: coroutine-to-coroutine transitions use symmetric transfer (see Task),
   * not schedule(). schedule() is used for (a) the initial launch of a detached
   * root and (b) future thread hops.
   */
  virtual void schedule(std::coroutine_handle<> handle) PURE;

  /**
   * Create a timer that invokes `cb` when it fires. Used by `TimerAwaitable` /
   * `sleep`, and later by the (deferred) timeout policy. Returns an Envoy
   * `Event::TimerPtr` whose destruction disarms the timer.
   */
  virtual Event::TimerPtr createTimer(std::function<void()> cb) PURE;
};

} // namespace Coroutine
} // namespace Envoy
