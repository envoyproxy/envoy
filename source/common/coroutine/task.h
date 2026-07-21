#pragma once

#include <concepts>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/coroutine/context.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Coroutine {

/**
 * This module implements a bare minimum set of classes for the Envoy to make
 * use of coroutine introduced in C++20.
 *
 * C++20 introduced language primitives (`co_await`, `co_return`, `co_yield`)
 * to support coroutine, but it doesn't include STL library to integrate the
 * primitives with an event loop / runtime environment because C++ doesn't have
 * a built in event loop.
 *
 * Given Envoy's siloed architecture, a small library like this enables
 * Envoy to make use of coroutine to start writing sequential-style code that's
 * more readable while preserving the event driven performance.
 *
 * Pulling in existing libraries like `Asio` and `libcoro` would bring in
 * unnecessary dependencies and/or a foreign event loop. This library should
 * only live until C++26 is widely adopted, which provides STL support of
 * coroutine in `std::execution`. It is intentionally kept small and lean so
 * that it is easier to maintain and migrate off eventually.
 */

// ---------------------------------------------------------------------------
// Task return-type constraint.
//
// A Task only ever completes via a `co_return`, and cancellation surfaces as a
// leaf resuming with an aborted `absl::Status` that the body propagates to that
// `co_return`. For a caller to tell "cancelled/failed" from "returned normally",
// the value itself has to carry that status -- so the only permitted
// return types are `absl::Status` and `absl::StatusOr<U>`.
// ---------------------------------------------------------------------------
template <class> inline constexpr bool kIsStatusOr = false;
template <class U> inline constexpr bool kIsStatusOr<absl::StatusOr<U>> = true;

template <class T>
concept TaskReturnType = std::same_as<T, absl::Status> || kIsStatusOr<T>;

// Forward declarations.
template <TaskReturnType T> class Task;
template <typename T> class LeafAwaitable;

// ---------------------------------------------------------------------------
// Awaitable classification concepts.
//
// These are the one vocabulary used to (a) restrict what a coroutine body may
// `co_await` on and (b) constrain what can be fed into `launch()` and the
// future `any_of`/`all_of` helpers.
//
// Using C++20 concepts and `static_assert` together to generate helpful compile
// time error messages.
// ---------------------------------------------------------------------------

// Strip const/volatile and reference qualifiers so a forwarding-reference
// parameter classifies by its value type.
template <class A> using bare = std::remove_cvref_t<A>;

template <class> inline constexpr bool kIsTask = false;
template <TaskReturnType T> inline constexpr bool kIsTask<Task<T>> = true;

// Only callable when its argument derives from LeafAwaitable<T> for some T (via a
// derived-to-base conversion that deduces T). Used to define the leaf classifier
// below without knowing the value type.
namespace Detail {
template <typename T> void leafAwaitableBaseProbe(const LeafAwaitable<T>&);
} // namespace Detail

template <class A>
concept TaskAwaitable = kIsTask<bare<A>>;

// A leaf is a type that publicly derives from LeafAwaitable<T> -- i.e. it carries
// the cancellation machinery (the pure-virtual onCancel, the setCancelCallback
// registration in the base's await_suspend). This is deliberately NOT a bare
// marker-base check: the classifier is tied to the machinery-bearing base so a
// type cannot be treated as a leaf while bypassing cancellation. Every leaf is
// cancellable, by construction of what "leaf" means here.
template <class A>
concept LeafAwaitable_ = requires(const bare<A>& a) { Detail::leafAwaitableBaseProbe(a); };

template <class A>
concept CoroAwaitable = TaskAwaitable<A> || LeafAwaitable_<A>;

// `static_assert(false, ...)` in a template is ill-formed even in an
// uninstantiated branch; gate it on a dependent value so it fires only when the
// fallback overload is actually selected.
template <class> inline constexpr bool dependent_false = false;

// ---------------------------------------------------------------------------
// Promise machinery shared by every coroutine type (Task<T> and the detached
// RootTask). Holds the two things propagated down the chain -- the context and
// the continuation -- and is the gate of every `co_await` call via
// `await_transform` to constrain the target of `co_await` to only TaskAwaitable
// and LeafAwaitable.
// ---------------------------------------------------------------------------
struct PromiseBase {
  // Always lazy start: the frame suspends at creation so its context can be set
  // before it runs (a child inherits it at `co_await`; a root gets it from launch()).
  std::suspend_always initial_suspend() noexcept { return {}; }

  // No exceptions on the data plane: errors travel as absl::Status values. A
  // coroutine that throws terminates the process.
  void unhandled_exception() noexcept { PANIC("coroutine threw on the data plane"); }

  // `co_await` interception point. If the promise of the current coroutine that calls
  // `co_await` has `await_transform` defined, the compiler routes every `co_await` through
  // it.
  //
  // We only allow LeafAwaitable and TaskAwaitable to be used.
  //
  // This also passes along the context.
  template <CoroAwaitable A> decltype(auto) await_transform(A&& a) {
    a.injectContext(context_);
    return std::forward<A>(a);
  }

  // Unconstrained fallback: chosen only when no constrained overload matches, so
  // a disallowed `co_await` gets a more useful compile time error message instead
  // of "no viable overload".
  template <class A> decltype(auto) await_transform(A&&) {
    static_assert(dependent_false<A>, "co_await is only allowed on a Task or a LeafAwaitable");
  }

  // Shared ownership keeps the context alive for detached frames.
  CoroutineContextPtr context_;
  // Where to resume to once the current coroutine `co_return`s. Generally pointing to a caller
  // that called `co_await` on the current coroutine (null for a root coroutine).
  std::coroutine_handle<> continuation_{};
};

// At final_suspend, transfer control back to the awaiting caller via symmetric
// transfer. A root has no continuation, so it suspends via noop_coroutine() so that it survives
// for the non-coroutine caller to inspect the result before destroying it.
struct FinalAwaiter {
  // Never directly resume execution.
  bool await_ready() const noexcept { return false; }

  // Compiler calls `co_await` on this struct which is returned from `final_suspend`. We want to
  // resume the awaiting caller coroutine if it exists, or suspend.
  template <typename P>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<P> me) noexcept {
    std::coroutine_handle<> cont = me.promise().continuation_;
    return cont ? cont : std::noop_coroutine();
  }

  // There is no return value.
  void await_resume() const noexcept {}
};

template <TaskReturnType T> struct TaskPromise : PromiseBase {
  Task<T> get_return_object();
  FinalAwaiter final_suspend() noexcept { return {}; }
  template <typename U> void return_value(U&& v) { result_.emplace(std::forward<U>(v)); }
  std::optional<T> result_;
};

// Awaitable produced by `co_await std::move(task)`.
//
// At suspend, symmetric-transfers into the new task and suspends the current coroutine.
template <TaskReturnType T> struct TaskAwaiter {
  std::coroutine_handle<TaskPromise<T>> child_;

  bool await_ready() const noexcept { return false; }

  template <typename ParentPromise>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> parent) noexcept {
    // Context was already injected into the child by the parent promise's
    // await_transform (the single propagation choke point). Here we only record
    // where the child returns to and hand it control.
    child_.promise().continuation_ = parent;
    return child_;
  }

  // [[nodiscard]]: the result carries success/failure/cancellation, so a
  // `co_await task;` that drops it is almost always a bug.
  [[nodiscard]] T await_resume() { return std::move(*child_.promise().result_); }
};

/**
 * Lazy, move-only coroutine type. `T` must be `absl::Status` or
 * `absl::StatusOr<U>` (see TaskReturnType). Awaiting a Task consumes it (via an
 * `&&`-qualified `operator co_await`), so it can also be moved into the future
 * `any_of`/`all_of` helpers (`any_of(std::move(t1), ...)`) without any change here.
 *
 * Marked [[nodiscard]]: a lazy Task that is created but never awaited or launched
 * runs nothing, so discarding the returned Task is a bug.
 */
template <TaskReturnType T> class [[nodiscard]] Task {
public:
  using promise_type = TaskPromise<T>;

  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { destroy(); }

  // Awaiting consumes the Task (move-only). Borrows the handle into the awaiter;
  // this Task value retains ownership and destroys the frame after the await.
  TaskAwaiter<T> operator co_await() && noexcept { return TaskAwaiter<T>{handle_}; }

  // Hand this coroutine its caller's context. Called by the parent promise's
  // await_transform -- the single propagation choke point (v1: inherit as-is).
  void injectContext(const CoroutineContextPtr& ctx) { handle_.promise().context_ = ctx; }

private:
  friend struct TaskPromise<T>;
  explicit Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

  void destroy() {
    if (handle_) {
      handle_.destroy();
    }
  }

  std::coroutine_handle<promise_type> handle_;
};

template <TaskReturnType T> Task<T> TaskPromise<T>::get_return_object() {
  return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
}

} // namespace Coroutine
} // namespace Envoy
