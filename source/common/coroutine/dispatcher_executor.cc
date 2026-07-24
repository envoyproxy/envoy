#include "source/common/coroutine/dispatcher_executor.h"

#include <coroutine>
#include <functional>
#include <utility>

namespace Envoy {
namespace Coroutine {

void DispatcherExecutor::schedule(std::coroutine_handle<> handle) {
  dispatcher_.post([handle]() { handle.resume(); });
}

Event::TimerPtr DispatcherExecutor::createTimer(std::function<void()> cb) {
  return dispatcher_.createTimer(std::move(cb));
}

} // namespace Coroutine
} // namespace Envoy
