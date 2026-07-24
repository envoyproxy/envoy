#include <coroutine>
#include <memory>
#include <optional>
#include <stdexcept>

#include "source/common/coroutine/context.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Coroutine {
namespace {

// ---------------------------------------------------------------------------
// A leaf whose completion the test drives directly. It records what the leaf saw
// (that it started, the executor its context carried) and lets the test complete
// or observe cancellation of the pending async op.
// ---------------------------------------------------------------------------
struct LeafController {
  bool started = false;
  bool cancelled = false;
  // When set, the leaf's onCancel() erroneously calls complete() -- a contract
  // violation used to exercise the ENVOY_BUG guard in LeafAwaitable::complete().
  bool complete_during_on_cancel = false;
  Executor* observed_executor = nullptr;
  // Valid while the leaf is the pending op; invoking it delivers a value.
  absl::AnyInvocable<void(absl::Status)> completer;

  void completeWith(absl::Status status) {
    ASSERT(completer);
    // Move out before invoking: completing resumes the coroutine inline, which
    // may destroy the leaf that owns the captured `this`.
    absl::AnyInvocable<void(absl::Status)> local = std::move(completer);
    completer = nullptr;
    local(std::move(status));
  }
};

class TestLeaf : public LeafAwaitable<absl::Status> {
public:
  explicit TestLeaf(LeafController& controller) : controller_(controller) {}

protected:
  void onStart() override {
    controller_.started = true;
    controller_.observed_executor = &context().executor();
    controller_.completer = [this](absl::Status status) { complete(std::move(status)); };
  }
  void onCancel() override {
    controller_.cancelled = true;
    controller_.completer = nullptr;
    if (controller_.complete_during_on_cancel) {
      // Illegal: completing here would make finish() resume the parent, which can
      // destroy this leaf before the cancel path's own finish() runs (UAF). The
      // guard must turn this into a no-op (ENVOY_BUG), not a second finish().
      complete(absl::OkStatus());
    }
  }

private:
  LeafController& controller_;
};

// Coroutines under test ------------------------------------------------------

Task<absl::StatusOr<int>> returnsValue(int value) { co_return value; }

Task<absl::Status> returnsOk(bool& ran) {
  ran = true;
  co_return absl::OkStatus();
}

// Throws from the coroutine body to exercise PromiseBase::unhandled_exception().
// `should_throw` is a runtime parameter so the `co_return` stays reachable (a bare
// `throw` before it would make the coroutine's return path dead code).
Task<absl::Status> throwsOnDataPlane(bool should_throw) {
  if (should_throw) {
    throw std::runtime_error("boom");
  }
  co_return absl::OkStatus();
}

Task<absl::Status> awaitLeaf(LeafController& controller) {
  TestLeaf leaf(controller);
  co_return co_await leaf;
}

// A chain N levels deep, ending in a leaf, to exercise context propagation.
Task<absl::Status> chainLevel0(LeafController& controller) {
  TestLeaf leaf(controller);
  co_return co_await leaf;
}
Task<absl::Status> chainLevel1(LeafController& controller) {
  co_return co_await chainLevel0(controller);
}
Task<absl::Status> chainLevel2(LeafController& controller) {
  co_return co_await chainLevel1(controller);
}

Task<absl::Status> awaitLeafWithCleanup(LeafController& controller, bool& cleaned_up) {
  absl::Cleanup guard = [&cleaned_up] { cleaned_up = true; };
  TestLeaf leaf(controller);
  co_return co_await leaf;
}

// ---------------------------------------------------------------------------
// CancellationState (milestone 2).
// ---------------------------------------------------------------------------
TEST(CancellationStateTest, DefaultNotCancelled) {
  CancellationState state;
  EXPECT_FALSE(state.cancelled());
}

TEST(CancellationStateTest, CancelSetsFlagAndIsIdempotent) {
  CancellationState state;
  int fired = 0;
  state.setCancelCallback([&fired] { ++fired; });
  state.cancel();
  EXPECT_TRUE(state.cancelled());
  EXPECT_EQ(1, fired);
  // Second cancel is a no-op and does not re-fire.
  state.cancel();
  EXPECT_EQ(1, fired);
}

TEST(CancellationStateTest, SetCallbackAfterCancelFiresSynchronously) {
  CancellationState state;
  state.cancel();
  int fired = 0;
  state.setCancelCallback([&fired] { ++fired; });
  EXPECT_EQ(1, fired);
}

TEST(CancellationStateTest, ClearedCallbackDoesNotFire) {
  CancellationState state;
  int fired = 0;
  state.setCancelCallback([&fired] { ++fired; });
  state.clearCancelCallback();
  state.cancel();
  EXPECT_EQ(0, fired);
}

// ---------------------------------------------------------------------------
// Task<T> / Task<void> (milestone 3), driven via launch().
// ---------------------------------------------------------------------------
TEST(TaskTest, ReturnsStatusOrValueThroughLaunch) {
  auto exec = std::make_shared<ManualExecutor>();
  std::optional<absl::StatusOr<int>> result;
  DetachedHandle handle =
      launch(returnsValue(42), exec, [&result](absl::StatusOr<int> value) { result = value; });
  EXPECT_FALSE(result.has_value()); // lazy: nothing runs until drained.
  exec->drain();
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->ok());
  EXPECT_EQ(42, **result);
}

TEST(TaskTest, ReturnsStatusThroughLaunch) {
  auto exec = std::make_shared<ManualExecutor>();
  bool body_ran = false;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(returnsOk(body_ran), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  EXPECT_TRUE(body_ran);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
}

TEST(TaskTest, DestroyingUnstartedTaskIsSafe) {
  LeafController controller;
  {
    Task<absl::Status> task = chainLevel0(controller);
    // Never awaited or launched; destructor frees the frame at initial_suspend.
  }
  EXPECT_FALSE(controller.started);
}

// An exception escaping a coroutine body is routed to unhandled_exception(), which
// panics: the data plane carries errors as absl::Status, never exceptions.
TEST(TaskTest, ThrowingOnDataPlanePanics) {
  auto exec = std::make_shared<ManualExecutor>();
  // launch + drain live entirely inside EXPECT_DEATH so the parent process starts
  // no coroutine (the throw aborts the forked child before the frame is freed).
  EXPECT_DEATH(
      {
        DetachedHandle handle = launch(throwsOnDataPlane(true), exec, [](absl::Status) {});
        exec->drain(); // resumes the body -> throws -> unhandled_exception() -> PANIC.
      },
      "coroutine threw on the data plane");
}

// promiseBase() recovers the shared PromiseBase (and thus the context) from a
// type-erased coroutine handle -- the seam an executor uses to inspect a handle.
TEST(PromiseBaseTest, RecoversContextFromTypeErasedHandle) {
  auto exec = std::make_shared<ManualExecutor>();
  bool ran = false;
  // A lazy, unstarted root; `root` retains ownership (we read the handle via
  // from_promise, not release()), so the frame is freed at scope exit.
  Detail::RootTask root = Detail::awaitTaskAndCallOnDone(returnsOk(ran), [](absl::Status) {});
  auto cancel = std::make_shared<CancellationState>();
  root.promise().context_ = std::make_shared<CoroutineContext>(exec, cancel);

  std::coroutine_handle<> handle =
      std::coroutine_handle<Detail::RootTask::promise_type>::from_promise(root.promise());
  EXPECT_EQ(&promiseBase(handle).context_->executor(), exec.get());
  EXPECT_EQ(promiseBase(handle).context_->cancellation().get(), cancel.get());
  EXPECT_FALSE(ran); // never started.
}

TEST(TaskTest, DeepChainPropagatesExecutorAndCancellation) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(chainLevel2(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  // Suspended at the deepest leaf; the leaf saw the launch executor, proving the
  // executor propagated three levels down.
  EXPECT_TRUE(controller.started);
  EXPECT_EQ(exec.get(), controller.observed_executor);
  EXPECT_FALSE(result.has_value());

  // Root cancel reaches the deepest leaf and unwinds the whole chain.
  handle.cancel();
  EXPECT_TRUE(controller.cancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*result));
}

// ---------------------------------------------------------------------------
// LeafAwaitable + cancellation (milestone 4).
// ---------------------------------------------------------------------------
TEST(LeafAwaitableTest, LeafCompletesWithValue) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  ASSERT_TRUE(controller.started);
  EXPECT_FALSE(result.has_value());

  controller.completeWith(absl::OkStatus());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
}

TEST(LeafAwaitableTest, CancelledWhilePendingFiresOnCancelAndUnwinds) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  ASSERT_TRUE(controller.started);

  handle.cancel();
  EXPECT_TRUE(controller.cancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*result));
}

// Calling complete() from within onCancel() is a contract violation: it must be
// flagged by ENVOY_BUG and swallowed. In a release build (where the bug only logs)
// the value passed to complete() is dropped and the chain still finishes with the
// cancellation status -- the guard prevents a second finish()/double-resume (UAF).
TEST(LeafAwaitableTest, CompleteDuringOnCancelIsEnvoyBugAndSwallowed) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  controller.complete_during_on_cancel = true;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  ASSERT_TRUE(controller.started);

  EXPECT_ENVOY_BUG(handle.cancel(), "complete() should not be called during onCancel()");

#if defined(NDEBUG) || defined(ENVOY_CONFIG_COVERAGE)
  // In release/coverage builds the ENVOY_BUG only logs, so cancel() ran to
  // completion in-process: the OkStatus from complete() was swallowed and the
  // chain finished with the cancellation status instead.
  EXPECT_TRUE(controller.cancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*result));
#else
  // In debug builds EXPECT_ENVOY_BUG forks and is fatal, so the parent process
  // never ran cancel(): the coroutine is still suspended at the leaf. Complete it
  // normally so the self-owned frame unwinds and is destroyed -- otherwise it (and
  // the context/executor it keeps alive) leaks, which asan flags.
  EXPECT_FALSE(result.has_value());
  controller.completeWith(absl::OkStatus());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
#endif
}

TEST(LeafAwaitableTest, PreCancelledScopeFailsFastWithoutStarting) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  // Cancel before the root ever runs: the first leaf await must fail fast.
  handle.cancel();
  exec->drain();
  EXPECT_FALSE(controller.started); // fail-fast: onStart never called.
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*result));
}

TEST(LeafAwaitableTest, CancelAfterCompleteIsNoOp) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  controller.completeWith(absl::OkStatus());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());

  // The chain already reached final_suspend; a late cancel does nothing and does
  // not re-invoke the completion callback.
  handle.cancel();
  EXPECT_FALSE(controller.cancelled);
  EXPECT_TRUE(result->ok());
}

// ---------------------------------------------------------------------------
// launch + DetachedHandle (milestone 5).
// ---------------------------------------------------------------------------
TEST(LaunchTest, EndToEndLeafToCallback) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  controller.completeWith(absl::InvalidArgumentError("boom"));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsInvalidArgument(*result));
}

TEST(LaunchTest, CancelMidFlightRunsDestructorsAndStillCallsBack) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  bool cleaned_up = false;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(awaitLeafWithCleanup(controller, cleaned_up), exec,
                                 [&result](absl::Status status) { result = std::move(status); });
  exec->drain();
  ASSERT_TRUE(controller.started);
  EXPECT_FALSE(cleaned_up);

  handle.cancel();
  // Unwinding runs the frame's RAII cleanup and still delivers the callback.
  EXPECT_TRUE(cleaned_up);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*result));
}

// The frame is self-owning, so destroying the handle from inside the done
// callback (before the frame reaches final_suspend) must be safe.
TEST(LaunchTest, DroppingHandleInsideOnDoneIsSafe) {
  auto exec = std::make_shared<ManualExecutor>();
  bool ran = false;
  bool called = false;
  std::optional<DetachedHandle> handle_slot;
  handle_slot = launch(returnsOk(ran), exec, [&](absl::Status status) {
    EXPECT_TRUE(status.ok());
    called = true;
    handle_slot.reset(); // destroy the handle mid-callback
  });
  exec->drain();
  EXPECT_TRUE(called);
  EXPECT_FALSE(handle_slot.has_value());
}

// Dropping the handle before the coroutine completes does not cancel or destroy
// it: the frame self-owns and runs to completion.
TEST(LaunchTest, DroppingHandleBeforeCompletionRunsToCompletion) {
  auto exec = std::make_shared<ManualExecutor>();
  bool ran = false;
  std::optional<absl::Status> result;
  {
    DetachedHandle handle = launch(returnsOk(ran), exec,
                                   [&result](absl::Status status) { result = std::move(status); });
    // handle dropped here, before draining.
  }
  exec->drain();
  EXPECT_TRUE(ran);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok()); // ran to completion, not cancelled.
}

// Dropping the handle while a leaf is still pending leaves the self-owned frame
// alive; it still completes when the leaf fires.
TEST(LaunchTest, HandleDroppedWhilePendingLeafStillCompletes) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  {
    DetachedHandle handle = launch(awaitLeaf(controller), exec,
                                   [&result](absl::Status status) { result = std::move(status); });
    exec->drain();
    ASSERT_TRUE(controller.started); // suspended at the leaf
    // handle dropped here, while the leaf is still pending.
  }
  EXPECT_FALSE(result.has_value());
  controller.completeWith(absl::OkStatus());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
}

// StartMode::Inline resumes the root on the caller's stack: a coroutine that never
// suspends runs to completion before launch() returns, with nothing scheduled.
TEST(LaunchTest, InlineStartRunsSynchronously) {
  auto exec = std::make_shared<ManualExecutor>();
  bool ran = false;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(
      returnsOk(ran), exec, [&result](absl::Status status) { result = std::move(status); },
      StartMode::Inline);
  // No drain(): the coroutine already completed inline, and nothing was posted.
  EXPECT_TRUE(ran);
  EXPECT_TRUE(exec->empty());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
}

// An inline start runs synchronously up to the first suspension (the leaf), again
// without scheduling anything; the leaf then drives completion as usual.
TEST(LaunchTest, InlineStartRunsUpToFirstSuspension) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController controller;
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(
      awaitLeaf(controller), exec, [&result](absl::Status status) { result = std::move(status); },
      StartMode::Inline);
  // Ran inline to the leaf without a drain(); nothing queued, still pending.
  EXPECT_TRUE(controller.started);
  EXPECT_TRUE(exec->empty());
  EXPECT_FALSE(result.has_value());

  controller.completeWith(absl::OkStatus());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
