#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "envoy/event/dispatcher.h"

#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Shared coroutine task lifecycle and stage queue pipeline machinery for RequestFilterManager
// and ResponseFilterManager.
//
// Manages N + 1 single-slot stage queues (0..N-1 for filters, N for the sink), inline task
// launching with handle tracking, single-invocation completion callback protection, and
// reentrancy-safe cancellation.
template <typename Item> class FilterPipeline {
public:
  using OnCompleteFn = absl::AnyInvocable<void(absl::Status)>;
  using StageQueue = Coroutine::AsyncQueue<Item>;
  using StageQueueSharedPtr = std::shared_ptr<StageQueue>;

  FilterPipeline(size_t num_filters, Event::Dispatcher& dispatcher,
                 OnCompleteFn on_complete = nullptr)
      : executor_(std::make_shared<Coroutine::DispatcherExecutor>(dispatcher)),
        on_complete_(std::move(on_complete)) {
    stages_.reserve(num_filters + 1);
    for (size_t i = 0; i <= num_filters; ++i) {
      stages_.push_back(std::make_shared<StageQueue>(/*max_size=*/1));
    }
  }

  virtual ~FilterPipeline() {
    if (!terminated_) {
      teardown(/*call_on_cancel=*/false);
    }
  }

  // Sets the completion callback if not provided at construction time.
  void setOnComplete(OnCompleteFn on_complete) { on_complete_ = std::move(on_complete); }

  bool terminated() const { return terminated_; }

  const std::shared_ptr<Coroutine::DispatcherExecutor>& executor() const { return executor_; }

  const StageQueueSharedPtr& stage(size_t index) const { return stages_[index]; }

  size_t numStages() const { return stages_.size(); }

  // Takes the next item for stage `index`; nullopt once the stage queue has been closed.
  Coroutine::Task<absl::StatusOr<std::optional<Item>>> receive(size_t index) {
    if (terminated_) {
      co_return absl::CancelledError("filter pipeline terminated");
    }
    co_return co_await stages_[index]->pop();
  }

  // Hands an item from stage `index` to the next stage (`index + 1`).
  Coroutine::Task<absl::Status> propagate(size_t index, Item item) {
    if (terminated_) {
      co_return absl::CancelledError("filter pipeline terminated");
    }
    co_return co_await stages_[index + 1]->push(std::move(item));
  }

  // Launches `task` with StartMode::Inline on the dispatcher executor and stores its handle
  // only if the pipeline has not terminated during launch.
  void launchTask(Coroutine::Task<absl::Status> task,
                  absl::AnyInvocable<void(absl::Status)> on_done) {
    Coroutine::DetachedHandle handle = Coroutine::launch(
        std::move(task), executor_, std::move(on_done), Coroutine::StartMode::Inline);
    // The task may have run to completion (and terminated the pipeline) before launch returned.
    if (!terminated_) {
      handles_.push_back(std::move(handle));
    }
  }

  // Marks the pipeline terminated and invokes `on_complete_` (at most once) with `status`.
  void complete(absl::Status status) {
    if (terminated_) {
      return;
    }
    terminated_ = true;
    if (on_complete_ != nullptr) {
      OnCompleteFn callback = std::move(on_complete_);
      on_complete_ = nullptr;
      callback(std::move(status));
    }
  }

  // Reentrancy-safe cancellation. Moves `handles_` into a local vector and cancels them before
  // closing stage queues so callbacks during coroutine frame destruction cannot re-enter or
  // corrupt `handles_`, and awaiting coroutines receive CancelledError rather than spurious EOF.
  // Clears `on_complete_` so external cancellation never invokes completion or retains captured
  // references.
  virtual void cancel() {
    if (terminated_) {
      return;
    }
    teardown(/*call_on_cancel=*/true);
  }

private:
  void teardown(bool call_on_cancel) {
    terminated_ = true;
    on_complete_ = nullptr;
    // Cancelling a handle may destroy a coroutine frame that holds the last reference to
    // something; drop them all at once before closing queues so awaiting coroutines unwind with
    // CancelledError rather than waking with std::nullopt (EOF).
    std::vector<Coroutine::DetachedHandle> handles = std::move(handles_);
    handles_.clear();
    for (Coroutine::DetachedHandle& handle : handles) {
      handle.cancel();
    }
    if (call_on_cancel) {
      onCancel();
    }
    for (auto& stage : stages_) {
      stage->close();
    }
  }

protected:
  // Hook invoked at the start of cancel() after `terminated_` is set to true, allowing derived
  // classes to clean up additional state (e.g. pending input buffers or signal queues) before
  // stages and coroutine handles are torn down.
  virtual void onCancel() {}

  // Extracts `on_complete_` (leaving `on_complete_ = nullptr` for single-invocation protection).
  // Use before `cancel()` when cancelling due to an error or local reply that must still
  // report completion status to the caller.
  OnCompleteFn takeOnComplete() {
    OnCompleteFn callback = std::move(on_complete_);
    on_complete_ = nullptr;
    return callback;
  }

  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  OnCompleteFn on_complete_;
  std::vector<StageQueueSharedPtr> stages_;
  std::vector<Coroutine::DetachedHandle> handles_;
  bool terminated_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
