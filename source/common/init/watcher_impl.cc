#include "common/init/watcher_impl.h"

namespace Envoy {
namespace Init {

WatcherHandleImpl::WatcherHandleImpl(absl::string_view handle_name, absl::string_view name,
                                     std::weak_ptr<ReadyFn> fn)
    : handle_name_(handle_name), name_(name), fn_has_parameter_(false), fn_(std::move(fn)) {}

WatcherHandleImpl::WatcherHandleImpl(absl::string_view handle_name, absl::string_view name,
                                     std::weak_ptr<ReadyFnSendName> fn)
    : handle_name_(handle_name), name_(name), fn_has_parameter_(true), fn_sn_(std::move(fn)) {}

bool WatcherHandleImpl::ready() const {
  if (fn_has_parameter_) {
    auto locked_fn(fn_sn_.lock());
    if (locked_fn) {
      // If we can "lock" a shared pointer to the watcher's callback function, call it.
      ENVOY_LOG(debug, "{} initialized, notifying {}", handle_name_, name_);
      (*locked_fn)(handle_name_);
      return true;
    } else {
      // If not, the watcher was already destroyed.
      ENVOY_LOG(debug, "{} initialized, but can't notify {} (unavailable)", handle_name_, name_);
      return false;
    }
  } else {
    auto locked_fn(fn_.lock());
    if (locked_fn) {
      // If we can "lock" a shared pointer to the watcher's callback function, call it.
      ENVOY_LOG(debug, "{} initialized, notifying {}", handle_name_, name_);
      (*locked_fn)();
      return true;
    } else {
      // If not, the watcher was already destroyed.
      ENVOY_LOG(debug, "{} initialized, but can't notify {} (unavailable)", handle_name_, name_);
      return false;
    }
  }
}

WatcherImpl::WatcherImpl(absl::string_view name, ReadyFn fn)
    : name_(name), fn_has_parameter_(false), fn_(std::make_shared<ReadyFn>(std::move(fn))) {}

WatcherImpl::WatcherImpl(absl::string_view name, ReadyFnSendName fn)
    : name_(name), fn_has_parameter_(true),
      fn_sn_(std::make_shared<ReadyFnSendName>(std::move(fn))) {}

WatcherImpl::~WatcherImpl() { ENVOY_LOG(debug, "{} destroyed", name_); }

absl::string_view WatcherImpl::name() const { return name_; }

WatcherHandlePtr WatcherImpl::createHandle(absl::string_view handle_name) const {
  // Note: can't use std::make_unique because WatcherHandleImpl ctor is private.
  if (fn_has_parameter_) {
    return std::unique_ptr<WatcherHandle>(
        new WatcherHandleImpl(handle_name, name_, std::weak_ptr<ReadyFnSendName>(fn_sn_)));
  } else {
    return std::unique_ptr<WatcherHandle>(
        new WatcherHandleImpl(handle_name, name_, std::weak_ptr<ReadyFn>(fn_)));
  }
}

} // namespace Init
} // namespace Envoy
