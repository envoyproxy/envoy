#include "common/init/manager_impl.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Init {

ManagerImpl::ManagerImpl(absl::string_view name)
    : name_(fmt::format("init manager {}", name)), state_(State::Uninitialized), count_(0),
      watcher_(name_, [this]() { onTargetReady(); }) {}

void ManagerImpl::maybeCreateNoopInitManager(Manager& local_init_manager, absl::string_view name,
                                             std::unique_ptr<Init::ManagerImpl>& init_manager,
                                             std::unique_ptr<Cleanup>& init) {
  if (local_init_manager.state() == Init::Manager::State::Initialized) {
    init_manager = std::make_unique<Init::ManagerImpl>(name);
    init = std::make_unique<Cleanup>([&init_manager, name] {
      // For new RDS subscriptions created after listener warming up, we don't wait for them to warm
      // up.
      Init::WatcherImpl noop_watcher(
          // Note: we just throw it away.
          fmt::format("{} ConfigUpdate watcher", name), []() { /*Do nothing.*/ });
      init_manager->initialize(noop_watcher);
    });
  }
}

Manager::State ManagerImpl::state() const { return state_; }

void ManagerImpl::add(const Target& target) {
  ++count_;
  TargetHandlePtr target_handle(target.createHandle(name_));
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
        onTargetReady();
      }
    }
  }
}

void ManagerImpl::onTargetReady() {
  // If there are no remaining targets and one mysteriously calls us back, this manager is haunted.
  ASSERT(count_ != 0, fmt::format("{} called back by target after initialization complete"));

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
