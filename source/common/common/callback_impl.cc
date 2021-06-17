#include "source/common/common/callback_impl.h"

namespace Envoy {
namespace Common {

ThreadSafeCallbackHandlePtr ThreadSafeCallbackManager::add(Event::Dispatcher& dispatcher,
                                                           Callback callback) {
  Thread::LockGuard lock(lock_);
  auto new_callback = std::make_shared<CallbackHolder>(*this, callback);
  callbacks_.push_back(CallbackListEntry(std::weak_ptr<CallbackHolder>(new_callback), dispatcher));
  // Get the list iterator of added callback handle, which will be used to remove itself from
  // callbacks_ list.
  new_callback->it_ = (--callbacks_.end());
  return new_callback;
}

void ThreadSafeCallbackManager::runCallbacks() {
  Thread::LockGuard lock(lock_);
  for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
    auto& [cb, cb_dispatcher] = *(it++);

    // sanity check cb is valid before attempting to schedule a dispatch
    if (cb.expired()) {
      continue;
    }

    cb_dispatcher.post([cb = cb] {
      // Once we're running on the thread that scheduled the callback, validate the
      // callback is still valid and execute
      std::shared_ptr<CallbackHolder> cb_shared = cb.lock();
      if (cb_shared != nullptr) {
        cb_shared->cb_();
      }
    });
  }
}

size_t ThreadSafeCallbackManager::size() const noexcept {
  Thread::LockGuard lock(lock_);
  return callbacks_.size();
}

void ThreadSafeCallbackManager::remove(typename std::list<CallbackListEntry>::iterator& it) {
  Thread::LockGuard lock(lock_);
  callbacks_.erase(it);
}

ThreadSafeCallbackManager::CallbackHolder::CallbackHolder(ThreadSafeCallbackManager& parent,
                                                          Callback cb)
    : cb_(cb), parent_dispatcher_(parent.dispatcher_), parent_(parent),
      still_alive_(parent.still_alive_) {}

ThreadSafeCallbackManager::CallbackHolder::~CallbackHolder() {
  parent_dispatcher_.post([still_alive = still_alive_, &parent = parent_, it = it_]() mutable {
    // We're running on the same thread the parent is managed on. We can assume there is
    // no race between checking if alive and calling remove.
    if (!still_alive.expired()) {
      parent.remove(it);
    }
  });
}

} // namespace Common
} // namespace Envoy
