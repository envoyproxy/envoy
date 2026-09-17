#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "envoy/event/dispatcher.h"

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Shared coroutine task lifecycle base class for RequestFilterManager::AsyncState and
// ResponseFilterManager::AsyncState.
//
// Manages inline task launching with handle tracking and reentrancy-safe handle cancellation.
class TaskGroup {
public:
  explicit TaskGroup(Event::Dispatcher& dispatcher)
      : executor_(std::make_shared<Coroutine::DispatcherExecutor>(dispatcher)) {}

  virtual ~TaskGroup() {
    if (!terminated_) {
      cancelHandles();
    }
  }

  bool terminated() const { return terminated_; }

protected:
  // Launches `task` with StartMode::Inline on the dispatcher executor and tracks its handle.
  // Does nothing if the group is already terminated. If the task triggers termination during its
  // initial inline execution and then suspends, its handle is immediately cancelled.
  void launchTask(Coroutine::Task<absl::Status> task,
                  absl::AnyInvocable<void(absl::Status)> on_done) {
    if (terminated_) {
      return;
    }
    Coroutine::DetachedHandle handle = Coroutine::launch(
        std::move(task), executor_, std::move(on_done), Coroutine::StartMode::Inline);
    if (terminated_) {
      handle.cancel();
      return;
    }
    handles_.push_back(std::move(handle));
  }

  // Marks the group terminated on normal completion without cancelling active handles.
  // Subsequent cancelHandles() calls (e.g. from ~TaskGroup() or stream teardown after completion)
  // become no-ops, allowing any filter coroutines doing post-propagation work to finish naturally.
  void markTerminated() { terminated_ = true; }

  // Reentrancy-safe handle cancellation. Marks the group terminated, moves `handles_` into a
  // local vector, and cancels them so callbacks during coroutine frame destruction cannot
  // re-enter or corrupt `handles_`.
  void cancelHandles() {
    terminated_ = true;
    std::vector<Coroutine::DetachedHandle> handles = std::move(handles_);
    handles_.clear();
    for (Coroutine::DetachedHandle& handle : handles) {
      handle.cancel();
    }
  }

private:
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  std::vector<Coroutine::DetachedHandle> handles_;
  bool terminated_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
