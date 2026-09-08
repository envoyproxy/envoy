#include "source/common/coroutine/dispatcher_executor.h"

#include <coroutine>
#include <functional>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/coroutine/task.h"

namespace Envoy {
namespace Coroutine {

void DispatcherExecutor::schedule(std::coroutine_handle<> handle) {
  // Debug-only invariant: a coroutine handed to this executor must belong to it.
  // Recover the shared context from the type-erased handle (every promise derives
  // from PromiseBase) and check that its executor is this one -- resuming a handle
  // whose chain expects a different executor would silently break the thread-siloing
  // guarantee.
  ASSERT(&promiseBase(handle).context_->executor() == this,
         "coroutine scheduled on an executor that does not match its context");
  dispatcher_.post([handle]() { handle.resume(); });
}

Event::TimerPtr DispatcherExecutor::createTimer(std::function<void()> cb) {
  return dispatcher_.createTimer(std::move(cb));
}

} // namespace Coroutine
} // namespace Envoy
