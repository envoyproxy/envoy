#include "source/common/init/manager_impl.h"

#include <functional>

#include "source/common/common/assert.h"
#include "source/common/init/watcher_impl.h"

namespace Envoy {
namespace Init {

ManagerImpl::ManagerImpl(absl::string_view name)
    : name_(fmt::format("init manager {}", name)),
      watcher_(name_, [this](absl::string_view target_name) { onTargetReady(target_name); }) {}

Manager::State ManagerImpl::state() const { return state_; }

void ManagerImpl::add(const Target& target) {
  ++count_;
  TargetHandlePtr target_handle(target.createHandle(name_));
  ++target_names_count_[target.name()];
  switch (state_) {
  case State::Uninitialized:
    // If the manager isn't initialized yet, save the target handle to be initialized later.
    ENVOY_LOG(debug, "added {} to {}", target.name(), name_);
    target_handles_.push_back(std::move(target_handle));
    return;
  case State::Initializing:
    // If the manager is already initializing, initialize the new target immediately. Note that
    // it's important in this case that count_ was incremented above before calling the target,
    // because if the target calls the init manager back immediately, count_ will be decremented
    // here (see the definition of watcher_ above).
    target_handle->initialize(watcher_);
    return;
  case State::Initialized:
    // If the manager has already completed initialization, consider this a programming error.
    ASSERT(false, fmt::format("attempted to add {} to initialized {}", target.name(), name_));
  }
}

void ManagerImpl::initialize(const Watcher& watcher) {
  // If the manager is already initializing or initialized, consider this a programming error.
  ASSERT(state_ == State::Uninitialized, fmt::format("attempted to initialize {} twice", name_));

  // Create a handle to notify when initialization is complete.
  watcher_handle_ = watcher.createHandle(name_);

  if (count_ == 0) {
    // If we have no targets, initialization trivially completes. This can happen, and is fine.
    ENVOY_LOG(debug, "{} contains no targets", name_);
    ready();
  } else {
    // If we have some targets, start initialization...
    ENVOY_LOG(debug, "{} initializing", name_);
    state_ = State::Initializing;

    // Attempt to initialize each target. If a target is unavailable, treat it as though it
    // completed immediately.
    for (const auto& target_handle : target_handles_) {
      if (!target_handle->initialize(watcher_)) {
        onTargetReady(target_handle->name());
      }
    }
  }
}

void ManagerImpl::dumpUnreadyTargets(envoy::admin::v3::UnreadyTargetsDumps& unready_targets_dumps) {
  auto& message = *unready_targets_dumps.mutable_unready_targets_dumps()->Add();
  message.set_name(name_);
  for (const auto& [target_name, count] : target_names_count_) {
    UNREFERENCED_PARAMETER(count);
    message.add_target_names(target_name);
  }
}

void ManagerImpl::onTargetReady(absl::string_view target_name) {
  // If there are no remaining targets and one mysteriously calls us back, this manager is haunted.
  ASSERT(count_ != 0,
         fmt::format("{} called back by target after initialization complete", target_name));

  // Decrease target_name count by 1.
  ASSERT(target_names_count_.find(target_name) != target_names_count_.end());
  if (--target_names_count_[target_name] == 0) {
    target_names_count_.erase(target_name);
  }

  // If there are no uninitialized targets remaining when called back by a target, that means it was
  // the last. Signal `ready` to the handle we saved in `initialize`.
  if (--count_ == 0) {
    ready();
  }
}

void ManagerImpl::ready() {
  state_ = State::Initialized;
  watcher_handle_->ready();
}

} // namespace Init
} // namespace Envoy
