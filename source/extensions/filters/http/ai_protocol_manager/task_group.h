#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"

#include "absl/container/btree_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Shared coroutine task lifecycle base class for RequestFilterManager::AsyncState and
// ResponseFilterManager::AsyncState.
//
// Manages inline task launching with handle tracking, automatic removal of completed handles,
// and reentrancy-safe handle cancellation.
class TaskGroup {
public:
  explicit TaskGroup(Event::Dispatcher& dispatcher)
      : executor_(std::make_shared<Coroutine::DispatcherExecutor>(dispatcher)),
        state_(std::make_shared<State>()) {}

  virtual ~TaskGroup() {
    if (!state_->terminated) {
      cancelHandles();
    }
  }

  bool terminated() const { return state_->terminated; }

protected:
  size_t handleCount() const { return state_->handles.size(); }

  // Launches `task` with StartMode::Inline on the dispatcher executor and tracks its handle
  // until completion. Does nothing if the group is already terminated. If the task completes
  // synchronously during its initial inline run, its handle is not retained. If the task
  // triggers termination during its initial inline run and then suspends, its handle is
  // immediately cancelled.
  void launchTask(Coroutine::Task<absl::Status> task,
                  absl::AnyInvocable<void(absl::Status)> on_done) {
    if (state_->terminated) {
      return;
    }
    std::shared_ptr<State> state = state_;
    const uint64_t id = state->next_id++;
    auto completed = std::make_shared<bool>(false);
    Coroutine::DetachedHandle handle = Coroutine::launch(
        std::move(task), executor_,
        [weak_state = std::weak_ptr<State>(state), id, completed,
         on_done = std::move(on_done)](absl::Status status) mutable {
          *completed = true;
          if (auto s = weak_state.lock()) {
            s->handles.erase(id);
          }
          on_done(std::move(status));
        },
        Coroutine::StartMode::Inline);
    if (*completed) {
      return;
    }
    if (state->terminated) {
      handle.cancel();
      return;
    }
    state->handles.emplace(id, std::move(handle));
  }

  // Marks the group terminated on normal completion without cancelling active handles.
  // Subsequent cancelHandles() calls (e.g. from ~TaskGroup() or stream teardown after completion)
  // become no-ops, allowing any filter coroutines doing post-propagation work to finish naturally.
  void markTerminated() { state_->terminated = true; }

  // Reentrancy-safe handle cancellation. Marks the group terminated, moves `handles` into a
  // local map, and cancels them in deterministic launch order so callbacks during coroutine
  // frame destruction cannot re-enter or corrupt `handles`.
  void cancelHandles() {
    state_->terminated = true;
    auto handles = std::move(state_->handles);
    state_->handles.clear();
    for (auto& [id, handle] : handles) {
      handle.cancel();
    }
  }

private:
  struct State {
    absl::btree_map<uint64_t, Coroutine::DetachedHandle> handles;
    uint64_t next_id{0};
    bool terminated{false};
  };

  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  std::shared_ptr<State> state_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
