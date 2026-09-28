#include "source/common/coroutine/backtrace.h"

#include "source/common/coroutine/task.h"

namespace Envoy {
namespace Coroutine {

bool Backtrace::log_to_stderr_ = false;
bool Backtrace::single_line_ = false;

const PromiseBase* Backtrace::getCurrentCoroutine() {
  return current_coroutine.load(std::memory_order_relaxed);
}

const PromiseBase* Backtrace::getCallersPromise(const PromiseBase& promise) {
  if (promise.continuation_) {
    return &promiseBase(promise.continuation_);
  }

  return nullptr;
}

const void* Backtrace::getCallerAddress(const PromiseBase& promise) { return promise.caller_; }

} // namespace Coroutine
} // namespace Envoy
