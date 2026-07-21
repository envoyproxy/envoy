#pragma once

#include <memory>

#include "source/common/coroutine/executor.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Coroutine {

/**
 * The cancellation state for a single `co_await` chain.
 *
 * At any instant a sequential chain has exactly at most one suspended leaf, so this
 * holds one active-leaf cancel callback. A leaf registers its cancel action while it
 * is suspended and clears it on resumption.
 *
 * TODO(penguingao): note for future: when structured concurrency is needed, each
 * block of concurrent tasks / awaitables (any_of, all_of) form a new leaf awaitable
 * in the current context. It creates a new context that has its own cancellation status.
 * The cancellation will be propagated by forwarding the callback to the new context's
 * `cancel()` method.
 */
class CancellationState {
public:
  bool cancelled() const { return cancelled_; }

  // Idempotent. Cancels the chain of coroutines and fires the registered leaf
  // callback if any.
  void cancel() {
    if (cancelled_) {
      return;
    }
    cancelled_ = true;
    /**
     * The callback is moved out of the member before it is invoked, so a
     * re-entrant `clearCancelCallback()` (which the leaf's one-shot `finish()`
     * calls) operates on an already-empty slot rather than destroying the callback
     * while it is running.
     */
    absl::AnyInvocable<void()> cb = std::move(on_cancel_);
    on_cancel_ = nullptr;
    if (cb) {
      cb();
    }
  }

  // Registers a cancellation callback. This is called by a leaf awaitable while it
  // is suspended. If the scope is already cancelled, the callback fires synchronously.
  void setCancelCallback(absl::AnyInvocable<void()> cb) {
    if (cancelled_) {
      cb();
      return;
    }
    on_cancel_ = std::move(cb);
  }

  // Clears the cancellation callback.
  void clearCancelCallback() { on_cancel_ = nullptr; }

private:
  bool cancelled_ = false;
  // At most one leaf is suspended.
  absl::AnyInvocable<void()> on_cancel_;
};

using CancellationStatePtr = std::shared_ptr<CancellationState>;

/**
 * The unit of propagation down a `co_await` chain. It carries the following contextual
 * properties that each coroutine inherits from its caller:
 *   - the executor (where/how to resume): each chain of coroutine runs on the same
 *     thread / executor.
 *   - the cancellation state: a chain of coroutine should be cancelled at the leaf
 *     awaitable, so that the call stack can properly unwind and clean up.
 *
 * Held by every promise as a `shared_ptr` so it stays alive until the root of the coroutine
 * `co_return`s.
 */
class CoroutineContext {
public:
  CoroutineContext(Executor* executor, CancellationStatePtr cancel)
      : executor_(executor), cancel_(std::move(cancel)) {}

  Executor& executor() const { return *executor_; }
  const CancellationStatePtr& cancellation() const { return cancel_; }

private:
  // Not owned.
  Executor* executor_;
  // One shared cancellation state across a `co_await` chain.
  CancellationStatePtr cancel_;
};

using CoroutineContextPtr = std::shared_ptr<CoroutineContext>;

} // namespace Coroutine
} // namespace Envoy
