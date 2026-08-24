#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>
#include <string>

#include "source/common/coroutine/any_of.h"
#include "source/common/coroutine/context.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Coroutine {
namespace {

// Controller and Leaf for testing Status-returning awaitables.
struct LeafController {
  bool started = false;
  bool cancelled = false;
  Executor* observed_executor = nullptr;
  absl::AnyInvocable<void(absl::Status)> completer;

  void completeWith(absl::Status status) {
    ASSERT(completer);
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
    controller_.observed_executor = &this->context().executor();
    controller_.completer = [this](absl::Status status) { complete(std::move(status)); };
  }
  void onCancel() override {
    controller_.cancelled = true;
    controller_.completer = nullptr;
  }

private:
  LeafController& controller_;
};

// Controller and Leaf for testing StatusOr<T>-returning awaitables.
template <typename T> struct LeafControllerVal {
  bool started = false;
  bool cancelled = false;
  Executor* observed_executor = nullptr;
  absl::AnyInvocable<void(absl::StatusOr<T>)> completer;

  void completeWith(absl::StatusOr<T> val) {
    ASSERT(completer);
    absl::AnyInvocable<void(absl::StatusOr<T>)> local = std::move(completer);
    completer = nullptr;
    local(std::move(val));
  }
};

template <typename T> class TestLeafVal : public LeafAwaitable<absl::StatusOr<T>> {
public:
  explicit TestLeafVal(LeafControllerVal<T>& controller) : controller_(controller) {}

protected:
  void onStart() override {
    controller_.started = true;
    controller_.observed_executor = &this->context().executor();
    controller_.completer = [this](absl::StatusOr<T> val) { this->complete(std::move(val)); };
  }
  void onCancel() override {
    controller_.cancelled = true;
    controller_.completer = nullptr;
  }

private:
  LeafControllerVal<T>& controller_;
};

// Coroutine helpers -----------------------------------------------------------

Task<absl::StatusOr<int>> immediateValueInt(int val) { co_return val; }

Task<absl::StatusOr<std::string>> immediateValueString(std::string val) { co_return val; }

Task<absl::Status> immediateOk() { co_return absl::OkStatus(); }

Task<absl::StatusOr<int>> immediateErrorInt(absl::Status error) { co_return error; }

Task<absl::Status> immediateErrorStatus(absl::Status error) { co_return error; }

Task<absl::StatusOr<int>> awaitLeafInt(LeafControllerVal<int>& controller) {
  TestLeafVal<int> leaf(controller);
  co_return co_await leaf;
}

Task<absl::StatusOr<std::string>> awaitLeafString(LeafControllerVal<std::string>& controller) {
  TestLeafVal<std::string> leaf(controller);
  co_return co_await leaf;
}

Task<absl::Status> awaitLeafStatus(LeafController& controller) {
  TestLeaf leaf(controller);
  co_return co_await leaf;
}

Task<absl::StatusOr<std::unique_ptr<int>>>
awaitLeafMoveOnly(LeafControllerVal<std::unique_ptr<int>>& controller) {
  TestLeafVal<std::unique_ptr<int>> leaf(controller);
  co_return co_await leaf;
}

// Deep chain helpers
Task<absl::StatusOr<int>> deepLevel0(LeafControllerVal<int>& controller) {
  TestLeafVal<int> leaf(controller);
  co_return co_await leaf;
}
Task<absl::StatusOr<int>> deepLevel1(LeafControllerVal<int>& controller) {
  co_return co_await deepLevel0(controller);
}
Task<absl::StatusOr<int>> deepLevel2(LeafControllerVal<int>& controller) {
  co_return co_await deepLevel1(controller);
}

Task<absl::StatusOr<int>> awaitLeafWithCleanup(LeafControllerVal<int>& controller,
                                               bool& cleaned_up) {
  absl::Cleanup guard = [&cleaned_up] { cleaned_up = true; };
  TestLeafVal<int> leaf(controller);
  co_return co_await leaf;
}

// Tests -----------------------------------------------------------------------

TEST(AnyOfTest, Branch0CompletesFirstAsync) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_FALSE(c0.cancelled);
  EXPECT_FALSE(c1.cancelled);
  EXPECT_FALSE(result.has_value());

  // Branch 0 completes first.
  c0.completeWith(42);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_EQ(absl::get<0>(result->value()), 42);

  // Branch 1 must be cancelled cleanly.
  EXPECT_TRUE(c1.cancelled);
  EXPECT_FALSE(c0.cancelled);
}

TEST(AnyOfTest, Branch1CompletesFirstAsync) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_FALSE(result.has_value());

  // Branch 1 completes first.
  c1.completeWith("hello envoy");

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  EXPECT_EQ(absl::get<1>(result->value()), "hello envoy");

  // Branch 0 must be cancelled cleanly.
  EXPECT_TRUE(c0.cancelled);
  EXPECT_FALSE(c1.cancelled);
}

TEST(AnyOfTest, Branch0ImmediateCompletion) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(immediateValueInt(100), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {}, StartMode::Inline);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_EQ(absl::get<0>(result->value()), 100);

  // Branch 1 was never even started because branch 0 finished immediately.
  EXPECT_FALSE(c1.started);
}

TEST(AnyOfTest, Branch1ImmediateCompletionAfterBranch0Suspends) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), immediateValueString("fast"));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {}, StartMode::Inline);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  EXPECT_EQ(absl::get<1>(result->value()), "fast");

  // Branch 0 was started and then immediately cancelled when branch 1 won.
  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c0.cancelled);
}

TEST(AnyOfTest, BothImmediateCompletion) {
  auto exec = std::make_shared<ManualExecutor>();

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(immediateValueInt(7), immediateValueString("world"));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {}, StartMode::Inline);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  // Branch 0 executes first and wins.
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_EQ(absl::get<0>(result->value()), 7);
}

TEST(AnyOfTest, Branch0FailsWithError) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  // Branch 0 completes first with an error.
  c0.completeWith(absl::InvalidArgumentError("bad request"));

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->ok());
  EXPECT_EQ(result->status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result->status().message(), "bad request");

  // Branch 1 is cancelled cleanly.
  EXPECT_TRUE(c1.cancelled);
}

TEST(AnyOfTest, Branch1FailsWithError) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  // Branch 1 completes first with an error.
  c1.completeWith(absl::InternalError("backend failure"));

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->ok());
  EXPECT_EQ(result->status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(result->status().message(), "backend failure");

  // Branch 0 is cancelled cleanly.
  EXPECT_TRUE(c0.cancelled);
}

TEST(AnyOfTest, ParentCancellationPropagatesToAllBranches) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_FALSE(c0.cancelled);
  EXPECT_FALSE(c1.cancelled);

  // Cancel parent.
  handle.cancel();

  // Both child branches must be cancelled.
  EXPECT_TRUE(c0.cancelled);
  EXPECT_TRUE(c1.cancelled);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(result->status()));
}

TEST(AnyOfTest, PreCancelledParentScopeFailsFast) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  // Cancel before draining / running.
  handle.cancel();
  exec->drain();

  // Leaves were never started.
  EXPECT_FALSE(c0.started);
  EXPECT_FALSE(c1.started);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(result->status()));
}

TEST(AnyOfTest, MoveOnlyReturnTypes) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<std::unique_ptr<int>> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<std::unique_ptr<int>, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafMoveOnly(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  // Complete branch 0 with a unique_ptr.
  c0.completeWith(std::make_unique<int>(999));

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  std::unique_ptr<int>& val_ptr = absl::get<0>(result->value());
  ASSERT_NE(val_ptr, nullptr);
  EXPECT_EQ(*val_ptr, 999);

  EXPECT_TRUE(c1.cancelled);
}

TEST(AnyOfTest, ThreeBranchesVariadic) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;
  LeafController c2;

  using ResultType = absl::StatusOr<absl::variant<int, std::string, absl::monostate>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafString(c1), awaitLeafStatus(c2));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_TRUE(c2.started);

  // Branch 1 completes first.
  c1.completeWith("branch 1 wins");

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  EXPECT_EQ(absl::get<1>(result->value()), "branch 1 wins");

  // Branches 0 and 2 are cancelled.
  EXPECT_TRUE(c0.cancelled);
  EXPECT_TRUE(c2.cancelled);
  EXPECT_FALSE(c1.cancelled);
}

TEST(AnyOfTest, FourBranchesVariadicBranch3Wins) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<int> c1;
  LeafControllerVal<int> c2;
  LeafControllerVal<int> c3;

  using ResultType = absl::StatusOr<absl::variant<int, int, int, int>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), awaitLeafInt(c1), awaitLeafInt(c2), awaitLeafInt(c3));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  // Branch 3 completes first.
  c3.completeWith(333);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 3);
  EXPECT_EQ(absl::get<3>(result->value()), 333);

  EXPECT_TRUE(c0.cancelled);
  EXPECT_TRUE(c1.cancelled);
  EXPECT_TRUE(c2.cancelled);
  EXPECT_FALSE(c3.cancelled);
}

TEST(AnyOfTest, LeafAwaitableDirectInput) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    // Pass TestLeafVal directly as awaitables into anyOf.
    result = co_await anyOf(TestLeafVal<int>(c0), TestLeafVal<std::string>(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);

  c0.completeWith(55);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_EQ(absl::get<0>(result->value()), 55);
  EXPECT_TRUE(c1.cancelled);
}

TEST(AnyOfTest, StatusReturnBranches) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController c0;
  LeafController c1;

  using ResultType = absl::StatusOr<absl::variant<absl::monostate, absl::monostate>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafStatus(c0), awaitLeafStatus(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  c1.completeWith(absl::OkStatus());

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  EXPECT_TRUE(c0.cancelled);
}

TEST(AnyOfTest, SnakeCaseAlias) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await any_of(awaitLeafInt(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  c0.completeWith(123);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_EQ(absl::get<0>(result->value()), 123);
  EXPECT_TRUE(c1.cancelled);
}

TEST(AnyOfTest, DeepChainBranchCancellation) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(deepLevel2(c0), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_EQ(c0.observed_executor, exec.get());

  c1.completeWith("done");

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  EXPECT_EQ(absl::get<1>(result->value()), "done");

  // Deep chain leaf is cancelled.
  EXPECT_TRUE(c0.cancelled);
}

TEST(AnyOfTest, DestructorCleanupOnCancelledBranch) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;
  bool cleaned_up = false;

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafWithCleanup(c0, cleaned_up), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_FALSE(cleaned_up);

  // Branch 1 completes -> branch 0 is cancelled and unwinds, running RAII destructors.
  c1.completeWith("winner");

  EXPECT_TRUE(cleaned_up);
  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
}

TEST(AnyOfTest, StatusMacrosIntegration) {
  auto exec = std::make_shared<ManualExecutor>();

  auto task_success = []() -> Task<absl::StatusOr<int>> {
    ASSIGN_OR_CO_RETURN(auto res,
                        co_await anyOf(immediateValueInt(42), immediateValueString("hi")));
    if (res.index() == 0) {
      co_return absl::get<0>(res) * 2;
    }
    co_return -1;
  };

  std::optional<absl::StatusOr<int>> result;
  DetachedHandle handle = launch(
      task_success(), exec, [&result](absl::StatusOr<int> val) { result = std::move(val); },
      StartMode::Inline);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(**result, 84);

  auto task_failure = []() -> Task<absl::StatusOr<int>> {
    ASSIGN_OR_CO_RETURN(auto res,
                        co_await anyOf(immediateErrorInt(absl::PermissionDeniedError("denied")),
                                       immediateValueString("hi")));
    co_return 0;
  };

  std::optional<absl::StatusOr<int>> fail_result;
  handle = launch(
      task_failure(), exec,
      [&fail_result](absl::StatusOr<int> val) { fail_result = std::move(val); }, StartMode::Inline);

  ASSERT_TRUE(fail_result.has_value());
  EXPECT_FALSE(fail_result->ok());
  EXPECT_EQ(fail_result->status().code(), absl::StatusCode::kPermissionDenied);
}

TEST(AnyOfTest, ImmediateOkStatus) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController c1;

  using ResultType = absl::StatusOr<absl::variant<absl::monostate, absl::monostate>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(immediateOk(), awaitLeafStatus(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {}, StartMode::Inline);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_FALSE(c1.started);
}

TEST(AnyOfTest, ImmediateErrorStatus) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafController c1;

  using ResultType = absl::StatusOr<absl::variant<absl::monostate, absl::monostate>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(immediateErrorStatus(absl::UnavailableError("service unavailable")),
                            awaitLeafStatus(c1));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {}, StartMode::Inline);

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->ok());
  EXPECT_EQ(result->status().code(), absl::StatusCode::kUnavailable);
  EXPECT_FALSE(c1.started);
}

TEST(AnyOfTest, NestedAnyOfInnerBranchWins) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<int> c1;
  LeafControllerVal<std::string> c2;

  using InnerResult = absl::variant<int, int>;
  using ResultType = absl::StatusOr<absl::variant<InnerResult, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(anyOf(awaitLeafInt(c0), awaitLeafInt(c1)), awaitLeafString(c2));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_TRUE(c2.started);

  // Inner branch 0 wins.
  c0.completeWith(42);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  const InnerResult& inner = absl::get<0>(result->value());
  EXPECT_EQ(inner.index(), 0);
  EXPECT_EQ(absl::get<0>(inner), 42);

  // Inner sibling (c1) and outer sibling (c2) must both be cancelled.
  EXPECT_TRUE(c1.cancelled);
  EXPECT_TRUE(c2.cancelled);
  EXPECT_FALSE(c0.cancelled);
}

TEST(AnyOfTest, NestedAnyOfOuterBranchWins) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<int> c1;
  LeafControllerVal<std::string> c2;

  using InnerResult = absl::variant<int, int>;
  using ResultType = absl::StatusOr<absl::variant<InnerResult, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(anyOf(awaitLeafInt(c0), awaitLeafInt(c1)), awaitLeafString(c2));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_TRUE(c2.started);

  // Outer branch (c2) wins.
  c2.completeWith("outer winner");

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  EXPECT_EQ(absl::get<1>(result->value()), "outer winner");

  // Both inner branches (c0 and c1) must be cancelled cleanly.
  EXPECT_TRUE(c0.cancelled);
  EXPECT_TRUE(c1.cancelled);
  EXPECT_FALSE(c2.cancelled);
}

TEST(AnyOfTest, NestedAnyOfBothBranchesNested) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<int> c1;
  LeafControllerVal<std::string> c2;
  LeafControllerVal<std::string> c3;

  using Inner0 = absl::variant<int, int>;
  using Inner1 = absl::variant<std::string, std::string>;
  using ResultType = absl::StatusOr<absl::variant<Inner0, Inner1>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(anyOf(awaitLeafInt(c0), awaitLeafInt(c1)),
                            anyOf(awaitLeafString(c2), awaitLeafString(c3)));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);
  EXPECT_TRUE(c2.started);
  EXPECT_TRUE(c3.started);

  // Branch 3 in inner group 1 wins.
  c3.completeWith("branch 3 wins");

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 1);
  const Inner1& inner1 = absl::get<1>(result->value());
  EXPECT_EQ(inner1.index(), 1);
  EXPECT_EQ(absl::get<1>(inner1), "branch 3 wins");

  EXPECT_TRUE(c0.cancelled);
  EXPECT_TRUE(c1.cancelled);
  EXPECT_TRUE(c2.cancelled);
  EXPECT_FALSE(c3.cancelled);
}

TEST(AnyOfTest, ReentrantParentCancellationDuringLosingBranchUnwinding) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<int> c0;
  LeafControllerVal<std::string> c1;
  std::optional<DetachedHandle> handle_opt;

  auto losing_task_with_parent_cancel =
      [&](LeafControllerVal<std::string>& controller) -> Task<absl::StatusOr<std::string>> {
    absl::Cleanup guard = [&] {
      // During cancellation of the losing branch, attempt to cancel parent.
      if (handle_opt.has_value()) {
        handle_opt->cancel();
      }
    };
    TestLeafVal<std::string> leaf(controller);
    co_return co_await leaf;
  };

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(awaitLeafInt(c0), losing_task_with_parent_cancel(c1));
    co_return absl::OkStatus();
  };

  handle_opt = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  EXPECT_TRUE(c0.started);
  EXPECT_TRUE(c1.started);

  // Branch 0 completes first. This will cancel branch 1, whose cleanup will cancel parent.
  // Must not trigger a double-resume or crash.
  c0.completeWith(12345);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);
  EXPECT_EQ(absl::get<0>(result->value()), 12345);
  EXPECT_TRUE(c1.cancelled);
}

TEST(AnyOfTest, SynchronousCancellationDuringChildStartup) {
  auto exec = std::make_shared<ManualExecutor>();
  LeafControllerVal<std::string> c1;
  std::optional<DetachedHandle> handle_opt;

  auto child_that_cancels_parent_synchronously = [&]() -> Task<absl::StatusOr<int>> {
    if (handle_opt.has_value()) {
      handle_opt->cancel();
    }
    co_return 100;
  };

  using ResultType = absl::StatusOr<absl::variant<int, std::string>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result = co_await anyOf(child_that_cancels_parent_synchronously(), awaitLeafString(c1));
    co_return absl::OkStatus();
  };

  handle_opt = launch(parent_task(), exec, [](absl::Status) {});
  exec->drain();

  ASSERT_TRUE(result.has_value());
  // Parent was cancelled fail-fast during startup.
  EXPECT_TRUE(absl::IsCancelled(result->status()));
  EXPECT_FALSE(c1.started);
}

class AnyOfSimulatedTimeTest : public testing::Test {
public:
  AnyOfSimulatedTimeTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        executor_(std::make_shared<DispatcherExecutor>(*dispatcher_)) {}

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<DispatcherExecutor> executor_;
};

TEST_F(AnyOfSimulatedTimeTest, TimerRaceFirstTimerWinsAndCancelsSecond) {
  using ResultType = absl::StatusOr<absl::variant<absl::monostate, absl::monostate>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result =
        co_await anyOf(sleep(std::chrono::milliseconds(20)), sleep(std::chrono::milliseconds(100)));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), executor_, [](absl::Status) {});
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(result.has_value());

  // Advance time past 20ms: timer 0 fires.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(25), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);

  ASSERT_TRUE(result.has_value());
  EXPECT_OK(*result);
  EXPECT_EQ(result->value().index(), 0);

  // Advance time past timer 1 deadline (100ms): timer 1 was disarmed, so nothing fires/crashes.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(150), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_OK(*result);
}

TEST_F(AnyOfSimulatedTimeTest, ParentCancellationDisarmsAllTimers) {
  using ResultType = absl::StatusOr<absl::variant<absl::monostate, absl::monostate>>;
  std::optional<ResultType> result;

  auto parent_task = [&]() -> Task<absl::Status> {
    result =
        co_await anyOf(sleep(std::chrono::milliseconds(50)), sleep(std::chrono::milliseconds(100)));
    co_return absl::OkStatus();
  };

  DetachedHandle handle = launch(parent_task(), executor_, [](absl::Status) {});
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(result.has_value());

  // Advance 10ms (neither timer fired yet).
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(result.has_value());

  // Cancel parent.
  handle.cancel();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(result->status()));

  // Advancing time past both deadlines must be completely clean.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(200), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(absl::IsCancelled(result->status()));
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
