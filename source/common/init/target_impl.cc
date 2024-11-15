#include "source/common/init/target_impl.h"

namespace Envoy {
namespace Init {

TargetHandleImpl::TargetHandleImpl(absl::string_view handle_name, absl::string_view name,
                                   std::weak_ptr<InternalInitializeFn> fn)
    : handle_name_(handle_name), name_(name), fn_(std::move(fn)) {}

bool TargetHandleImpl::initialize(const Watcher& watcher) const {
  auto locked_fn(fn_.lock());
  if (locked_fn) {
    // If we can "lock" a shared pointer to the target's callback function, call it
    // with a new handle to the ManagerImpl's watcher that was passed in.
    ENVOY_LOG(debug, "{} initializing {}", handle_name_, name_);
    (*locked_fn)(watcher.createHandle(name_));
    return true;
  } else {
    // If not, the target was already destroyed.
    ENVOY_LOG(debug, "{} can't initialize {} (unavailable)", handle_name_, name_);
    return false;
  }
}

absl::string_view TargetHandleImpl::name() const { return name_; }

TargetImpl::TargetImpl(absl::string_view name, InitializeFn fn)
    : name_(fmt::format("target {}", name)),
      fn_(std::make_shared<InternalInitializeFn>([this, fn](WatcherHandlePtr watcher_handle) {
        watcher_handle_ = std::move(watcher_handle);
        fn();
      })) {}

TargetImpl::~TargetImpl() { ENVOY_LOG(debug, "{} destroyed", name_); }

absl::string_view TargetImpl::name() const { return name_; }

TargetHandlePtr TargetImpl::createHandle(absl::string_view handle_name) const {
  // Note: can't use std::make_unique here because TargetHandleImpl ctor is private.
  return TargetHandlePtr(
      new TargetHandleImpl(handle_name, name_, std::weak_ptr<InternalInitializeFn>(fn_)));
}

bool TargetImpl::ready() {
  if (watcher_handle_) {
    // If we have a handle for the ManagerImpl's watcher, signal it and then reset so it can't be
    // accidentally signaled again.
    // NOTE: We must move watcher_handle_ to a local to avoid the scenario in which as a result of
    // calling ready() this target is destroyed. This is possible in practice, for example when
    // a listener is deleted as a result of a failure in the context of the ready() call.
    auto local_watcher_handle = std::move(watcher_handle_);
    return local_watcher_handle->ready();
  }
  return false;
}

SharedTargetImpl::SharedTargetImpl(absl::string_view name, InitializeFn fn)
    : name_(fmt::format("shared target {}", name)),
      fn_(std::make_shared<InternalInitializeFn>([this, fn](WatcherHandlePtr watcher_handle) {
        if (initialized_) {
          watcher_handle->ready();
        } else {
          watcher_handles_.push_back(std::move(watcher_handle));
          std::call_once(once_flag_, fn);
        }
      })) {}

SharedTargetImpl::~SharedTargetImpl() { ENVOY_LOG(debug, "{} destroyed", name_); }

absl::string_view SharedTargetImpl::name() const { return name_; }

TargetHandlePtr SharedTargetImpl::createHandle(absl::string_view handle_name) const {
  // Note: can't use std::make_unique here because TargetHandleImpl ctor is private.
  return TargetHandlePtr(
      new TargetHandleImpl(handle_name, name_, std::weak_ptr<InternalInitializeFn>(fn_)));
}

bool SharedTargetImpl::ready() {
  initialized_ = true;
  // NOTE: We must move watcher_handles_ to a local to avoid the scenario in which as a result of
  // calling ready() this target is destroyed. This is possible in practice, for example when
  // a listener is deleted as a result of a failure in the context of the ready() call.
  auto local_watcher_handles = std::move(watcher_handles_);
  bool all_notified = !local_watcher_handles.empty();
  for (auto& watcher_handle : local_watcher_handles) {
    all_notified = watcher_handle->ready() && all_notified;
  }
  return all_notified;
}

} // namespace Init
} // namespace Envoy
