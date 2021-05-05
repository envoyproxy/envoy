#pragma once

#include <functional>
#include <list>

#include "envoy/common/callback.h"
#include "envoy/thread/thread.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Common {

/**
 * Utility class for managing callbacks.
 */
template <typename... CallbackArgs> class CallbackManager {
public:
  using Callback = std::function<void(CallbackArgs...)>;

  /**
   * Add a callback.
   * @param callback supplies the callback to add.
   * @return CallbackHandlePtr a handle that can be used to remove the callback.
   */
  ABSL_MUST_USE_RESULT CallbackHandlePtr add(Callback callback) {
    auto new_callback = std::make_unique<CallbackHolder>(*this, callback);
    callbacks_.emplace_back(new_callback.get());
    // Get the list iterator of added callback handle, which will be used to remove itself from
    // callbacks_ list.
    new_callback->it_ = (--callbacks_.end());
    return new_callback;
  }

  /**
   * Run all registered callbacks.
   * NOTE: This code is currently safe if a callback deletes ITSELF from within a callback. It is
   *       not safe if a callback deletes other callbacks. If that is required the code will need
   *       to change (specifically, it will crash if the next callback in the list is deleted).
   * @param args supplies the callback arguments.
   */
  void runCallbacks(CallbackArgs... args) {
    for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
      auto current = *(it++);
      current->cb_(args...);
    }
  }

private:
  struct CallbackHolder : public CallbackHandle {
    CallbackHolder(CallbackManager& parent, Callback cb)
        : parent_(parent), cb_(cb), still_alive_(parent.still_alive_) {}
    ~CallbackHolder() override {
      // Here we check if our parent manager is still alive via the still alive weak reference.
      // This code is single threaded so expired() is sufficient for our purpose. Assuming the
      // weak reference is not expired it is safe to remove ourselves from the manager.
      if (!still_alive_.expired()) {
        parent_.remove(it_);
      }
    }

    CallbackManager& parent_;
    Callback cb_;
    const std::weak_ptr<bool> still_alive_;

    // The iterator of this callback holder inside callbacks_ list
    // upon removal, use this iterator to delete callback holder in O(1).
    typename std::list<CallbackHolder*>::iterator it_;
  };

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(typename std::list<CallbackHolder*>::iterator& it) { callbacks_.erase(it); }

  std::list<CallbackHolder*> callbacks_;
  // This is a sentinel shared_ptr used for keeping track of whether the manager is still alive.
  // It is only held by weak reference in the callback holder above. This is used versus having
  // the manager inherit from shared_from_this to avoid the manager having to be allocated inside
  // a shared_ptr at all call sites.
  const std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
};

/**
 * Utility class for managing callbacks. Thread-safe.
 * @see CallbackManager
 */
template <typename... CallbackArgs> class ThreadSafeCallbackManager {
public:
  using Callback = std::function<void(CallbackArgs...)>;

  ~ThreadSafeCallbackManager() {
    // Validate against use-after-free
    ASSERT(callbacks_.empty());
  }

  /**
   * Add a callback.
   * @param callback supplies the callback to add.
   * @return CallbackHandlePtr a handle that can be used to remove the callback.
   */
  ABSL_MUST_USE_RESULT CallbackHandlePtr add(Callback callback) {
    Thread::LockGuard lock(lock_);
    auto new_callback = std::make_unique<CallbackHolder>(*this, callback);
    callbacks_.emplace_back(new_callback.get());
    // Get the list iterator of added callback handle, which will be used to remove itself from
    // callbacks_ list.
    new_callback->it_ = (--callbacks_.end());
    return new_callback;
  }

  /**
   * Run all registered callbacks.
   * NOTE: This code is currently safe if a callback deletes ITSELF from within a callback. It is
   *       not safe if a callback deletes other callbacks. If that is required the code will need
   *       to change (specifically, it will crash if the next callback in the list is deleted).
   * @param args supplies the callback arguments.
   */
  void runCallbacks(CallbackArgs... args) {
    Thread::LockGuard lock(lock_);
    for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
      auto current = *(it++);
      current->cb_(args...);
    }
  }

  /**
   * Run all registered callbacks with a custom caller
   * NOTE: This code does not currently support callbacks deleting itself or other registered
   *       callbacks as it will create a deadlock within the callback manager.
   * @param run_with function that is responsible for executing the callbacks and supplying any
   *                 inputs.
   */
  void runCallbacksWith(std::function<void(Callback)> run_with) {
    Thread::LockGuard lock(lock_);
    for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
      auto current = *(it++);
      run_with(current->cb_);
    }
  }

  size_t size() const noexcept {
    Thread::LockGuard lock(lock_);
    return callbacks_.size();
  }

private:
  struct CallbackHolder : public CallbackHandle {
    CallbackHolder(ThreadSafeCallbackManager& parent, Callback cb)
        : parent_(parent), cb_(cb), still_alive_(parent.still_alive_) {}
    ~CallbackHolder() override {
      // Here we check if our parent manager is still alive via the still alive weak reference.
      // This code is single threaded so expired() is sufficient for our purpose. Assuming the
      // weak reference is not expired it is safe to remove ourselves from the manager.
      if (!still_alive_.expired()) {
        parent_.remove(it_);
      }
    }

    ThreadSafeCallbackManager& parent_;
    Callback cb_;
    const std::weak_ptr<bool> still_alive_;

    // The iterator of this callback holder inside callbacks_ list
    // upon removal, use this iterator to delete callback holder in O(1).
    typename std::list<CallbackHolder*>::iterator it_;
  };

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(typename std::list<CallbackHolder*>::iterator& it) {
    Thread::LockGuard lock(lock_);
    callbacks_.erase(it);
  }

  // This must be held on all read/writes of callbacks_
  mutable Thread::MutexBasicLockable lock_;

  std::list<CallbackHolder*> callbacks_ ABSL_GUARDED_BY(lock_);
  // This is a sentinel shared_ptr used for keeping track of whether the manager is still alive.
  // It is only held by weak reference in the callback holder above. This is used versus having
  // the manager inherit from shared_from_this to avoid the manager having to be allocated inside
  // a shared_ptr at all call sites.
  const std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
};

} // namespace Common
} // namespace Envoy
