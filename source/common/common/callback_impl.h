#pragma once

#include "common/common/assert.h"

namespace Envoy {
namespace Upstream {

/**
 * Utility class for managing callbacks.
 */
template <class Callback, typename... CallbackArgs> class CallbackManager {
public:
  /**
   * Add a callback.
   * @param callback supplies the callback to add.
   * @return const CallbackHandle* a handle that can be used to remove the callback.
   */
  CallbackHandle* add(Callback callback) {
    callbacks_.emplace_back(*this, callback);
    return &callbacks_.back();
  }

  /**
   * Run all registered callbacks.
   * @param args supplies the callback arguments.
   */
  void runCallbacks(CallbackArgs... args) {
    for (const auto& cb : callbacks_) {
      cb.cb_(args...);
    }
  }

private:
  struct UpdateCbHolder : public CallbackHandle {
    UpdateCbHolder(CallbackManager& parent, Callback cb) : parent_(parent), cb_(cb) {}

    // CallbackHandle
    void remove() override { parent_.remove(this); }

    CallbackManager& parent_;
    Callback cb_;
  };

  /**
   * Remove a member update callback added via add().
   * @param handle supplies the callback handle to remove.
   */
  void remove(CallbackHandle* handle) {
    ASSERT(std::find_if(callbacks_.begin(), callbacks_.end(),
                        [handle](const UpdateCbHolder& holder) -> bool {
                          return handle == &holder;
                        }) != callbacks_.end());
    callbacks_.remove_if(
        [handle](const UpdateCbHolder& holder) -> bool { return handle == &holder; });
  }

  std::list<UpdateCbHolder> callbacks_;
};

} // namespace Upstream
} // namespace Envoy
