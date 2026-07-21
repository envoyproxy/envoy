#pragma once

#include <chrono>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>

#include "envoy/common/pure.h"

#include "source/common/coroutine/context.h"
#include "source/common/coroutine/task.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Coroutine {

/**
 * Base class every leaf (timer wait, eventfd, c-ares, openssl callback, a stream
 * operation) derives from to bridge a callback/OS event into a cancellable
 * awaitable. Its job is exactly three things: fail-fast when pre-cancelled,
 * one-shot completion, and cancellation registration.
 *
 * It deliberately does NOT own a timer or implement timeout: timeout is a policy
 * applied around a leaf (deferred), not baked into every leaf.
 *
 * `T` is the value the await produces. Cancellation resumes the await with an
 * aborted status, so `T` must be constructible from an `absl::Status` (e.g.
 * `absl::Status` itself or `absl::StatusOr<X>`); the body then propagates that
 * status to a normal `co_return`.
 *
 * Contract for derived types: `onStart()` must arrange asynchronous completion.
 * It must not call `complete()` synchronously within `onStart()` -- the frame is
 * mid-suspend at that point and resuming it inline would be undefined.
 */
template <typename T> class LeafAwaitable {
  static_assert(std::is_constructible_v<T, absl::Status>,
                "a LeafAwaitable value type must be constructible from absl::Status so a "
                "cancellation can be delivered as an aborted value");

public:
  // Fail-fast: if the scope is already cancelled, don't even start.
  bool await_ready() { return context_->cancellation()->cancelled(); }

  void await_suspend(std::coroutine_handle<> continuation) {
    continuation_ = continuation;
    // Register the cancel action while this is the pending leaf.
    context_->cancellation()->setCancelCallback([this] {
      onCancel();
      finish(abortedValue());
    });
    onStart(); // derived kicks off the async op; must eventually call complete().
  }

  // [[nodiscard]]: the result carries success/failure/cancellation, so a
  // `co_await leaf;` that drops it is almost always a bug.
  [[nodiscard]] T await_resume() {
    // On the fail-fast path (await_ready true) await_suspend never ran, so
    // result_ is empty and we resume with the aborted value.
    return result_ ? std::move(*result_) : abortedValue();
  }

  // Hand this leaf its awaiting coroutine's context (called by the promise's
  // await_transform).
  void injectContext(const CoroutineContextPtr& ctx) { context_ = ctx; }

protected:
  // Derived implements these:
  virtual void onStart() PURE;  // launch the op; arrange to call complete(value).
  virtual void onCancel() PURE; // cancel the pending op (honor its cancel contract).

  // Called by derived when the real event fires.
  void complete(T value) { finish(std::move(value)); }

  CoroutineContext& context() { return *context_; }

  virtual ~LeafAwaitable() = default;

private:
  static T abortedValue() { return absl::CancelledError("coroutine cancelled"); }

  void finish(T value) {
    if (finished_) {
      return; // one-shot: cancel/complete race -> first wins.
    }
    finished_ = true;
    context_->cancellation()->clearCancelCallback();
    result_ = std::move(value);
    // Inline resume at the event-loop boundary. Must touch no members after
    // this: resuming may run the coroutine to completion and destroy this leaf.
    if (continuation_) {
      continuation_.resume();
    }
  }

  bool finished_ = false;
  std::optional<T> result_;
  std::coroutine_handle<> continuation_{};
  // Captured from the awaiting promise; OWNS a share of the context.
  CoroutineContextPtr context_;
};

/**
 * The one concrete leaf in core: waits for a timer to fire. It is the only type
 * that touches a timer, and it doubles as `sleep(d)`.
 */
class TimerAwaitable : public LeafAwaitable<absl::Status> {
public:
  explicit TimerAwaitable(std::chrono::milliseconds duration) : duration_(duration) {}

protected:
  void onStart() override {
    timer_ = context().executor().createTimer([this] { complete(absl::OkStatus()); });
    timer_->enableTimer(duration_);
  }
  // Destroying the timer disarms it, honoring the Event::Timer cancel contract.
  void onCancel() override { timer_.reset(); }

private:
  Event::TimerPtr timer_;
  std::chrono::milliseconds duration_;
};

/**
 * Suspends the awaiting coroutine for `duration`.
 *
 * e.g. co_await sleep(500ms)
 * */
inline TimerAwaitable sleep(std::chrono::milliseconds duration) { return TimerAwaitable(duration); }

} // namespace Coroutine
} // namespace Envoy
