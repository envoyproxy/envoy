#pragma once

#include <functional>
#include <list>

#include "envoy/common/callback.h"

#include "common/common/assert.h"

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
   * @return CallbackHandle* a handle that can be used to remove the callback.
   */
  CallbackHandle* add(Callback callback) {
    callbacks_.emplace_back(*this, callback);
    // get the list iterator of added callback handle, which will be used to remove itself from
    // callbacks_ list.
    callbacks_.back().it_ = (--callbacks_.end());
    return &callbacks_.back();
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
      auto current = it++;
      current->cb_(args...);
    }
  }

private:
  struct CallbackHolder : public CallbackHandle {
    CallbackHolder(CallbackManager& parent, Callback cb) : parent_(parent), cb_(cb) {}

    // CallbackHandle
    void remove() override { parent_.remove(it_); }

    CallbackManager& parent_;
    Callback cb_;

    // the iterator of this callback holder inside callbacks_ list
    // upon removal, use this iterator to delete callback holder in O(1)
    typename std::list<CallbackHolder>::iterator it_;
  };

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(typename std::list<CallbackHolder>::iterator& it) { callbacks_.erase(it); }

  std::list<CallbackHolder> callbacks_;
};

} // namespace Common
} // namespace Envoy
