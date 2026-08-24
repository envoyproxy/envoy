#pragma once

#include <concepts>
#include <coroutine>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/coroutine/context.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Coroutine {

// Mapping from Task return type to the value type inside the result variant.
template <typename T> struct TaskValueType;

template <typename U> struct TaskValueType<absl::StatusOr<U>> {
  using type = U;
};

template <> struct TaskValueType<absl::Status> {
  using type = absl::monostate;
};

template <typename T> using TaskValueTypeT = typename TaskValueType<T>::type;

namespace Detail {

class AnyOfStateBase {
public:
  virtual ~AnyOfStateBase() = default;

  void cancelAll();
  void cancelChildrenExcept(size_t winner_index);

  CancellationStatePtr parent_cancel;
  std::coroutine_handle<> parent_continuation{};
  bool starting{false};
  bool finished{false};
  std::vector<CancellationStatePtr> child_cancels;
};

template <size_t I, typename Variant, typename T>
absl::StatusOr<Variant> makeAnyOfResult(T child_result) {
  if constexpr (kIsStatusOr<std::remove_cvref_t<T>>) {
    if (!child_result.ok()) {
      return child_result.status();
    }
    return Variant(absl::in_place_index<I>, std::move(child_result).value());
  } else {
    static_assert(std::same_as<std::remove_cvref_t<T>, absl::Status>,
                  "Task return type must be absl::Status or absl::StatusOr<U>");
    if (!child_result.ok()) {
      return child_result;
    }
    return Variant(absl::in_place_index<I>, absl::monostate{});
  }
}

template <typename... Ts> class AnyOfState : public AnyOfStateBase {
public:
  using VariantType = absl::variant<TaskValueTypeT<Ts>...>;
  using ResultType = absl::StatusOr<VariantType>;

  template <size_t I, typename T> void onChildDone(T child_result) {
    if (finished) {
      return;
    }
    finished = true;
    result = makeAnyOfResult<I, VariantType>(std::move(child_result));

    if (parent_cancel) {
      parent_cancel->clearCancelCallback();
    }

    cancelChildrenExcept(I);

    if (!starting) {
      std::coroutine_handle<> cont = std::exchange(parent_continuation, {});
      if (cont) {
        cont.resume();
      }
    }
  }

  std::optional<ResultType> result;
};

template <size_t Index, typename T, typename StatePtr>
RootTask runChildTask(Task<T> task, StatePtr state) {
  T result = co_await std::move(task);
  state->template onChildDone<Index>(std::move(result));
}

template <TaskAwaitable T> T toTask(T task) { return task; }

template <LeafAwaitable_ A> Task<decltype(std::declval<A>().await_resume())> toTask(A awaitable) {
  co_return co_await std::move(awaitable);
}

} // namespace Detail

template <TaskReturnType... Ts>
class AnyOfAwaitable : public LeafAwaitable<absl::StatusOr<absl::variant<TaskValueTypeT<Ts>...>>> {
public:
  using VariantType = absl::variant<TaskValueTypeT<Ts>...>;
  using ResultType = absl::StatusOr<VariantType>;

  explicit AnyOfAwaitable(Task<Ts>... tasks) : tasks_(std::move(tasks)...) {}

  AnyOfAwaitable(AnyOfAwaitable&&) noexcept = default;
  AnyOfAwaitable& operator=(AnyOfAwaitable&&) noexcept = default;
  AnyOfAwaitable(const AnyOfAwaitable&) = delete;
  AnyOfAwaitable& operator=(const AnyOfAwaitable&) = delete;
  ~AnyOfAwaitable() override = default;

  bool await_suspend(std::coroutine_handle<> continuation) {
    if (this->context().cancellation()->cancelled()) {
      return false;
    }

    auto state = std::make_shared<Detail::AnyOfState<Ts...>>();
    state_ = state;
    state->parent_continuation = continuation;
    state->parent_cancel = this->context().cancellation();
    state->child_cancels.resize(sizeof...(Ts));

    // Register parent cancellation callback.
    this->context().cancellation()->setCancelCallback([state] {
      state->cancelAll();
      if (!state->starting) {
        std::coroutine_handle<> cont = std::exchange(state->parent_continuation, {});
        if (cont) {
          cont.resume();
        }
      }
    });

    state->starting = true;
    startChildren(state, std::index_sequence_for<Ts...>{});
    state->starting = false;

    if (state->finished) {
      this->context().cancellation()->clearCancelCallback();
      state->parent_continuation = {};
      local_result_ = std::move(state->result);
      return false;
    }

    return true;
  }

  [[nodiscard]] ResultType await_resume() {
    if (local_result_.has_value()) {
      return std::move(*local_result_);
    }
    if (state_ && state_->result.has_value()) {
      return std::move(*state_->result);
    }
    return absl::CancelledError("coroutine cancelled");
  }

protected:
  void onStart() override {}
  void onCancel() override {
    if (state_) {
      state_->cancelAll();
    }
  }

private:
  template <size_t... Is>
  void startChildren(const std::shared_ptr<Detail::AnyOfState<Ts...>>& state,
                     std::index_sequence<Is...>) {
    (startOneChild<Is>(state), ...);
  }

  template <size_t I> void startOneChild(const std::shared_ptr<Detail::AnyOfState<Ts...>>& state) {
    if (state->finished) {
      return;
    }
    auto child_cancel = std::make_shared<CancellationState>();
    state->child_cancels[I] = child_cancel;

    auto child_ctx =
        std::make_shared<CoroutineContext>(this->context().executorShared(), child_cancel);

    auto child_root = Detail::runChildTask<I>(std::move(std::get<I>(tasks_)), state);
    child_root.promise().context_ = std::move(child_ctx);
    child_root.release().resume();
  }

  std::tuple<Task<Ts>...> tasks_;
  std::shared_ptr<Detail::AnyOfState<Ts...>> state_;
  std::optional<ResultType> local_result_;
};

namespace Detail {

template <TaskReturnType... Ts> AnyOfAwaitable<Ts...> makeAnyOfAwaitable(Task<Ts>... tasks) {
  return AnyOfAwaitable<Ts...>(std::move(tasks)...);
}

} // namespace Detail

/**
 * Structured concurrency primitive that runs multiple tasks/awaitables concurrently
 * and completes as soon as the first branch completes.
 *
 * When one branch completes, all other branches are immediately cancelled via their
 * cancellation callbacks.
 *
 * Returns absl::StatusOr<absl::variant<TaskValueTypeT<Ts>...>> containing the winning branch value
 * or an error status if the winning branch failed or the parent was cancelled.
 */
template <CoroAwaitable... As>
  requires(sizeof...(As) >= 2)
auto anyOf(As&&... awaitables) {
  return Detail::makeAnyOfAwaitable(Detail::toTask(std::forward<As>(awaitables))...);
}

template <CoroAwaitable... As>
  requires(sizeof...(As) >= 2)
auto any_of(As&&... awaitables) {
  return anyOf(std::forward<As>(awaitables)...);
}

} // namespace Coroutine
} // namespace Envoy
