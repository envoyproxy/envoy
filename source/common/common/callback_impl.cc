#include "source/common/common/callback_impl.h"

namespace Envoy {
namespace Common {

CallbackHandlePtr ThreadSafeCallbackManager::add(Event::Dispatcher& dispatcher, Callback callback) {
  Thread::LockGuard lock(lock_);
  auto new_callback = std::make_unique<CallbackHolder>(shared_from_this(), callback, dispatcher);
  callbacks_.push_back(CallbackListEntry(new_callback.get(), dispatcher,
                                         std::weak_ptr<bool>(new_callback->still_alive_)));
  // Get the list iterator of added callback handle, which will be used to remove itself from
  // callbacks_ list.
  new_callback->it_ = (--callbacks_.end());
  return new_callback;
}

void ThreadSafeCallbackManager::runCallbacks() {
  Thread::LockGuard lock(lock_);
  for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
    auto& [cb, cb_dispatcher, still_alive] = *(it++);

    cb_dispatcher.post([cb = cb, still_alive = still_alive] {
      // Once we're running on the thread that scheduled the callback, validate the
      // callback is still valid and execute. Even though 'expired()' is racy, because
      // we are on the scheduling thread, this should not race with destruction.
      if (!still_alive.expired()) {
        cb->cb_();
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

ThreadSafeCallbackManager::CallbackHolder::CallbackHolder(
    std::shared_ptr<ThreadSafeCallbackManager> parent, Callback cb,
    Event::Dispatcher& cb_dispatcher)
    : parent_(parent), cb_(cb), callback_dispatcher_(cb_dispatcher) {}

ThreadSafeCallbackManager::CallbackHolder::~CallbackHolder() {
  // Validate that destruction of the callback is happening on the same thread in which it was
  // intended to be executed.
  ASSERT(callback_dispatcher_.isThreadSafe());
  parent_->remove(it_);
}

} // namespace Common
} // namespace Envoy
