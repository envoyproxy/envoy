#pragma once

#include <coroutine>
#include <type_traits>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/coroutine/context.h"
#include "source/common/coroutine/executor.h"
#include "source/common/coroutine/task.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Coroutine {

/**
 * Owns a root coroutine frame and exposes cancellation over it. This is handed
 * back from `launch()` so non-coroutine code can start a coroutine and receive a
 * callback signaling that the coroutine has finished. The handle can also be used
 * to cancel the coroutine. The done callback will be called even if the coroutine
 * is cancelled.
 *
 * Only safe to destruct once the done callback is fired.
 */
class DetachedHandle {
public:
  DetachedHandle(std::coroutine_handle<> root, CoroutineContextPtr context)
      : root_(root), context_(std::move(context)) {}

  // Move only.
  DetachedHandle(DetachedHandle&& other) noexcept
      : root_(std::exchange(other.root_, {})), context_(std::move(other.context_)) {}
  DetachedHandle& operator=(DetachedHandle&& other) noexcept {
    if (this != &other) {
      reset();
      root_ = std::exchange(other.root_, {});
      context_ = std::move(other.context_);
    }
    return *this;
  }
  DetachedHandle(const DetachedHandle&) = delete;
  DetachedHandle& operator=(const DetachedHandle&) = delete;
  ~DetachedHandle() { reset(); }

  // Requests cancellation of the coroutine chain. Fires the pending leaf's cancel
  // callback, which resumes the chain with an aborted status; it then unwinds
  // fail-fast to a normal `co_return`. Cancellation is advisory -- the frame is
  // freed only when the coroutine reaches `final_suspend`.
  void cancel() {
    if (context_) {
      context_->cancellation()->cancel();
    }
  }

  // Whether the coroutine has reached final_suspend.
  bool done() const { return root_ && root_.done(); }

private:
  void reset() {
    if (root_) {
      // v1: destroying a not-yet-done frame is unsafe; the registry adopts that
      // case later. In supported flows the chain resumes inline to completion
      // before the owner is dropped.
      ASSERT(root_.done());
      root_.destroy();
      root_ = {};
    }
  }

  std::coroutine_handle<> root_;
  CoroutineContextPtr context_;
};

namespace Detail {

/**
 * A special internal coroutine type that helps implementing the done callback. It
 * uses the same promise type of `Task`, but its frame handle is held by the
 * `DetachedHandle` returned from `launch()`.
 *
 * This exists so that we can implement the done callback as `on_done(co_await task)`,
 * so that we avoid polluting the PromiseBase's `final_suspend` with the on_done
 * callback.
 */
class RootTask {
public:
  struct promise_type : PromiseBase {
    RootTask get_return_object() {
      return RootTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    // A root has no parent to transfer to, so it simply parks at final_suspend:
    // the frame survives (done() == true) for the DetachedHandle to inspect and
    // destroy. (Task uses FinalAwaiter instead, to symmetric-transfer back to its
    // awaiting parent -- a root would just return control to the executor.)
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
  };

  // Move-only frame carrier: it is constructed by get_return_object(), returned
  // from awaitTaskAndCallOnDone(), and has its handle released into a DetachedHandle -- it is
  // never copied or assigned. The move constructor keeps it move-only (and covers
  // the coroutine return-object path); no assignment operator is needed.
  RootTask(RootTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  RootTask(const RootTask&) = delete;
  ~RootTask() {
    if (handle_) {
      handle_.destroy();
    }
  }

  promise_type& promise() { return handle_.promise(); }

  // Relinquish frame ownership to the caller (the DetachedHandle).
  std::coroutine_handle<promise_type> release() { return std::exchange(handle_, {}); }

private:
  friend struct promise_type;
  explicit RootTask(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

  std::coroutine_handle<promise_type> handle_;
};

// The root coroutine body: awaits the user task (reusing all Task/awaiter
// machinery) and invokes the completion callback with its result (an absl::Status
// or StatusOr<U>). `OnDone` is deduced rather than spelled `AnyInvocable<void(T)>`
// only so the callback's argument type does not have to be named twice.
template <typename T, typename OnDone>
RootTask awaitTaskAndCallOnDone(Task<T> task, OnDone on_done) {
  on_done(co_await std::move(task));
}

// Give the root its context, hand its frame to a DetachedHandle, and schedule the
// (lazy) start.
inline DetachedHandle startRoot(RootTask root, Executor& exec) {
  auto ctx = std::make_shared<CoroutineContext>(&exec, std::make_shared<CancellationState>());
  root.promise().context_ = ctx;
  std::coroutine_handle<> handle = root.release();
  exec.schedule(handle);
  return DetachedHandle(handle, std::move(ctx));
}

} // namespace Detail

/**
 * Start `task` on `exec` from non-coroutine code. `on_done` is invoked with the
 * task's result when the chain completes (including after a cancellation, which
 * delivers an aborted result). Returns a `DetachedHandle` that owns the frame and
 * can `cancel()` it.
 *
 * `std::type_identity_t` puts the callback parameter in a non-deduced context so
 * `T` is deduced only from `task` (an `absl::Status` or `StatusOr<U>`), letting a
 * plain lambda convert to the callback type.
 */
template <typename T>
DetachedHandle launch(Task<T> task, Executor& exec,
                      std::type_identity_t<absl::AnyInvocable<void(T)>> on_done) {
  return Detail::startRoot(Detail::awaitTaskAndCallOnDone(std::move(task), std::move(on_done)),
                           exec);
}

} // namespace Coroutine
} // namespace Envoy
