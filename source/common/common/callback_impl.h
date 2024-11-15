#pragma once

#include <functional>
#include <list>
#include <memory>
#include <tuple>

#include "envoy/common/callback.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread/thread.h"

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Common {

/**
 * Utility class for managing callbacks.
 *
 * @see ThreadSafeCallbackManager for dealing with callbacks across multiple threads
 */
template <typename... CallbackArgs> class CallbackManager {
public:
  using Callback = std::function<absl::Status(CallbackArgs...)>;

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
  absl::Status runCallbacks(CallbackArgs... args) {
    for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
      auto current = *(it++);
      RETURN_IF_NOT_OK(current->cb_(args...));
    }
    return absl::OkStatus();
  }

  /**
   * @brief Run all callbacks with a function that returns the input arguments
   *
   * NOTE: This code is currently safe if a callback deletes ITSELF from within a callback. It is
   *       not safe if a callback deletes other callbacks.
   * @param run_with function that is responsible for generating inputs to callbacks. This will be
   * executed once for each callback.
   */
  absl::Status runCallbacksWith(std::function<std::tuple<CallbackArgs...>(void)> run_with) {
    for (auto it = callbacks_.cbegin(); it != callbacks_.cend();) {
      auto cb = *(it++);
      RETURN_IF_NOT_OK(std::apply(cb->cb_, run_with()));
    }
    return absl::OkStatus();
  }

  size_t size() const noexcept { return callbacks_.size(); }

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
 * @see CallbackManager for a non-thread-safe version
 */
class ThreadSafeCallbackManager : public std::enable_shared_from_this<ThreadSafeCallbackManager> {
  struct CallbackHolder;
  using CallbackListEntry = std::tuple<CallbackHolder*, Event::Dispatcher&, std::weak_ptr<bool>>;

public:
  using Callback = std::function<void()>;

  /**
   * @brief Create a ThreadSafeCallbackManager
   *
   * @note The ThreadSafeCallbackManager must always be represented as a std::shared_ptr in
   *       order to satisfy internal conditions to how callbacks are managed.
   */
  static std::shared_ptr<ThreadSafeCallbackManager> create() {
    return std::shared_ptr<ThreadSafeCallbackManager>(new ThreadSafeCallbackManager());
  }

  /**
   * @brief Add a callback.
   * @param dispatcher Dispatcher from the same thread as the registered callback. This will be used
   *                   to schedule the execution of the callback.
   * @param callback callback to add
   * @return Handle that can be used to remove the callback.
   */
  ABSL_MUST_USE_RESULT CallbackHandlePtr add(Event::Dispatcher& dispatcher, Callback callback);

  /**
   * @brief Run all callbacks
   */
  void runCallbacks();

  size_t size() const noexcept;

private:
  struct CallbackHolder : public CallbackHandle {
    CallbackHolder(std::shared_ptr<ThreadSafeCallbackManager> parent, Callback cb,
                   Event::Dispatcher& cb_dispatcher);

    ~CallbackHolder() override;

    std::shared_ptr<ThreadSafeCallbackManager> parent_;
    Callback cb_;
    Event::Dispatcher& callback_dispatcher_;
    std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};

    typename std::list<CallbackListEntry>::iterator it_;
  };

  ThreadSafeCallbackManager() = default;

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(typename std::list<CallbackListEntry>::iterator& it);

  // This must be held on all read/writes of callbacks_
  mutable Thread::MutexBasicLockable lock_{};

  std::list<CallbackListEntry> callbacks_ ABSL_GUARDED_BY(lock_);
};

} // namespace Common
} // namespace Envoy
