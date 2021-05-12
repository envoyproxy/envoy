#pragma once

#include <functional>
#include <list>
#include <tuple>

#include "envoy/common/callback.h"
#include "envoy/event/dispatcher.h"
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
 * @brief Utility class for managing callbacks across multiple threads.
 *
 * This is essentially a thread-safe version of the CallbackManager, but APIs to differ in some key
 * regards. Specifically this class makes use of the Event::Dispatcher to coordinate events across
 * threads and does involve some locking when it comes to updating the internally managed callback
 * list.
 *
 * @note This is not safe to use for instances in which the lifetimes of the threads registering
 * callbacks is less than the thread that owns the callback manager due to an assumption that
 * dispatchers registered alongside callbacks remain valid, even if the callback expires.
 *
 * @see CallbackManager
 */
template <typename... CallbackArgs> class ThreadSafeCallbackManager {
  struct CallbackHolder;
  using CallbackListEntry = std::tuple<std::weak_ptr<CallbackHolder>, Event::Dispatcher&>;

public:
  using Callback = std::function<void(CallbackArgs...)>;

  /**
   * @param dispatcher Dispatcher relevant to the thread in which the callback manager is
   * created/managed
   */
  explicit ThreadSafeCallbackManager(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  ~ThreadSafeCallbackManager() {
    // Validate against use-after-free
    ASSERT(callbacks_.empty());
  }

  /**
   * @brief Add a callback.

   * @param dispatcher Dispatcher from the same thread as the registered callback. This will be used
   *                   to schedule the execution of the callback.
   * @param callback callback to add
   * @return ThreadSafeCallbackHandlePtr a handle that can be used to remove the callback.
   */
  ABSL_MUST_USE_RESULT ThreadSafeCallbackHandlePtr add(Event::Dispatcher& dispatcher,
                                                       Callback callback) {
    Thread::LockGuard lock(lock_);
    auto new_callback = std::make_shared<CallbackHolder>(*this, callback);
    callbacks_.push_back(CallbackListEntry(std::weak_ptr(new_callback), dispatcher));
    // Get the list iterator of added callback handle, which will be used to remove itself from
    // callbacks_ list.
    new_callback->it_ = (--callbacks_.end());
    return new_callback;
  }

  /**
   * @brief Run all callbacks with a function that returns the input arguments
   *
   * NOTE: This code is currently safe if a callback deletes ITSELF from within a callback. It is
   *       not safe if a callback deletes other callbacks.

   * @param run_with function that is responsible for generating inputs to callbacks. This will be
   * executed for each callback. Return values must be able to be safely copied across threads and
   * asynchronously consumed.
   */
  void runCallbacksWith(std::function<std::tuple<CallbackArgs...>(void)> run_with) {
    Thread::LockGuard lock(lock_);
    for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
      auto& [cb, cb_dispatcher] = *(it++);

      // sanity check cb is valid before attempting to schedule a dispatch
      if (cb.expired()) {
        continue;
      }

      auto args = run_with();
      cb_dispatcher.post([cb = cb, args = args] {
        // Once we're running on the thread that scheduled the callback, validate the
        // callback is still valid and execute
        std::shared_ptr<CallbackHolder> cb_shared = cb.lock();
        if (cb_shared != nullptr) {
          std::apply(cb_shared->cb_, std::move(args));
        }
      });
    }
  }

  size_t size() const noexcept {
    Thread::LockGuard lock(lock_);
    return callbacks_.size();
  }

private:
  struct CallbackHolder : public CallbackHandle {
    CallbackHolder(ThreadSafeCallbackManager& parent, Callback cb)
        : cb_(cb), parent_dispatcher_(parent.dispatcher_), parent_(parent),
          still_alive_(parent.still_alive_) {}

    ~CallbackHolder() override {
      // schedule the removal on the parent thread to avoid cross-thread access or lock contention
      parent_dispatcher_.post([still_alive = still_alive_, &parent = parent_, it = it_]() mutable {
        if (!still_alive.expired()) {
          parent.remove(it);
        }
      });
    }

    Callback cb_;
    Event::Dispatcher& parent_dispatcher_;

    ThreadSafeCallbackManager& parent_;
    std::weak_ptr<bool> still_alive_;
    typename std::list<CallbackListEntry>::iterator it_;
  };

  Event::Dispatcher& dispatcher_;

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(typename std::list<CallbackListEntry>::iterator& it) {
    Thread::LockGuard lock(lock_);
    callbacks_.erase(it);
  }

  // This must be held on all read/writes of callbacks_
  mutable Thread::MutexBasicLockable lock_;

  std::list<CallbackListEntry> callbacks_ ABSL_GUARDED_BY(lock_);

  // This is a sentinel shared_ptr used for keeping track of whether the manager is still alive.
  // It is only held by weak reference in the callback holder above. This is used versus having
  // the manager inherit from shared_from_this to avoid the manager having to be allocated inside
  // a shared_ptr at all call sites.
  const std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
};

} // namespace Common
} // namespace Envoy
