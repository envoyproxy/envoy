#pragma once

#include <coroutine>
#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/coroutine/executor.h"

namespace Envoy {
namespace Coroutine {

/**
 * `Executor` backed by an Envoy `Event::Dispatcher`. This is the production
 * executor: coroutines resume on the same dispatcher as their caller, keeping
 * them compatible with the rest of Envoy's thread-siloed code.
 *
 * Holds the dispatcher by reference (no ownership): a `Dispatcher` outlives every
 * request on its thread.
 */
class DispatcherExecutor : public Executor {
public:
  explicit DispatcherExecutor(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  // schedule() -> post(), which is thread-safe: the seam for future thread hops.
  void schedule(std::coroutine_handle<> handle) override;
  Event::TimerPtr createTimer(std::function<void()> cb) override;

private:
  Event::Dispatcher& dispatcher_;
};

} // namespace Coroutine
} // namespace Envoy
