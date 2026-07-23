#pragma once

#include <coroutine>
#include <type_traits>
#include <utility>

#include "source/common/coroutine/context.h"
#include "source/common/coroutine/executor.h"
#include "source/common/coroutine/task.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Coroutine {

/**
 * A cancellation handle for a coroutine started by `launch()`.
 *
 * The launched coroutine owns its own frame and self-destroys when it reaches
 * `final_suspend` (see RootTask), so this handle does NOT own the frame -- it only
 * holds a share of the cancellation state. That is what makes it safe to destroy
 * at any time: dropping the handle does not cancel the coroutine (it keeps running
 * to completion) and can never destroy a frame at the wrong moment.
 *
 * Call `cancel()` to request cancellation; the chain unwinds fail-fast and the
 * done callback passed to `launch()` still fires (with an aborted status).
 */
class DetachedHandle {
public:
  explicit DetachedHandle(CancellationStatePtr cancel) : cancel_(std::move(cancel)) {}

  // Move-only handle semantics.
  DetachedHandle(DetachedHandle&&) noexcept = default;
  DetachedHandle& operator=(DetachedHandle&&) noexcept = default;
  DetachedHandle(const DetachedHandle&) = delete;
  DetachedHandle& operator=(const DetachedHandle&) = delete;

  // Requests cancellation of the coroutine chain. Fires the pending leaf's cancel
  // callback, which resumes the chain with an aborted status; it then unwinds
  // fail-fast to a normal `co_return`. The done callback still fires.
  void cancel() {
    if (cancel_) {
      cancel_->cancel();
    }
  }

private:
  CancellationStatePtr cancel_;
};

namespace Detail {

/**
 * The internal root coroutine type. It awaits the user task and invokes the done
 * callback (see awaitTaskAndCallOnDone), then self-destroys at final_suspend. It
 * owns its own frame -- no external owner has to destroy it at the right moment,
 * which is what lets DetachedHandle be dropped at any time.
 *
 * It exists so the done callback can be expressed as `on_done(co_await task)`,
 * reusing the normal await machinery instead of a bespoke `final_suspend` in
 * PromiseBase. Users only ever see `launch()` and `DetachedHandle`.
 */
class RootTask {
public:
  struct promise_type : PromiseBase {
    RootTask get_return_object() {
      return RootTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    // Self-owning: on_done has already run in the body, so completion is signaled
    // before the frame goes away. suspend_never destroys the frame here, so no
    // external owner ever has to. (Task uses FinalAwaiter instead, to
    // symmetric-transfer back to its awaiting parent.)
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
  };

  // Move-only frame carrier: it is constructed by get_return_object(), returned
  // from awaitTaskAndCallOnDone(), and has its handle released (to be scheduled and
  // then self-owned) -- it is never copied or assigned. The move constructor keeps
  // it move-only (and covers the coroutine return-object path); no assignment
  // operator is needed.
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

// Give the root its context and schedule its (lazy) start. The frame self-owns
// from here; the returned handle only carries the cancellation state.
inline DetachedHandle startRoot(RootTask root, Executor& exec) {
  auto cancel = std::make_shared<CancellationState>();
  root.promise().context_ = std::make_shared<CoroutineContext>(&exec, cancel);
  exec.schedule(root.release());
  return DetachedHandle(std::move(cancel));
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
[[nodiscard]] DetachedHandle launch(Task<T> task, Executor& exec,
                                    std::type_identity_t<absl::AnyInvocable<void(T)>> on_done) {
  return Detail::startRoot(Detail::awaitTaskAndCallOnDone(std::move(task), std::move(on_done)),
                           exec);
}

} // namespace Coroutine
} // namespace Envoy
